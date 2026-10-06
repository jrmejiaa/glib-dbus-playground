# 006 - The event stream is lossless; polling after the fact races

## The lesson

When systemd owns revival (`Restart=on-failure`), a crashed unit is restarted
within milliseconds. That makes **how** you observe the failure decisive:

- **Push is lossless.** D-Bus signals are queued on the connection socket and
    delivered **in order** when your event loop next turns. A busy or briefly
    blocked loop *delays* processing; it never *drops* messages. A subscribed
    signal carries the value **as it was at emission time**, frozen in the
    message — a later overwrite cannot reach back into an already-queued message.
- **Pull races.** `Result`/`ExecMainStatus` are **last-run snapshots**, not a
    history. If you react to a change by issuing a fresh `Get`, your read lands
    *after* the auto-restart and the fresh healthy run has already reset those
    properties to `success`/`0`. The failure cause is gone.

The discipline that follows: **consume the event and read every value out of the
signal payload (or the proxy cache the signal just updated); never re-`Get` a
property after being woken. This is the empirical backing for ADR 0006.

Two experiments below prove it, and both ship as runnable scripts in
[`scripts/`](./scripts). Running them exactly as written reproduces the results.

## Experiment A - queued signals survive a blocked loop

**Question:** if the loop is briefly busy while systemd crashes-and-restarts a
unit, does the `failed` transition and its cause get lost?

**Method** ([`scripts/race-probe.py`](./scripts/race-probe.py)): call the
Manager's `Subscribe()` (without it systemd emits nothing — ADR 0004), subscribe
to `JobRemoved` + per-unit `PropertiesChanged`, `SIGKILL` the main process, then
**deliberately block the event loop for 1 full second** across the entire
crash+auto-restart cycle (`RestartSec=100ms`, the worst case).

### Reproduce

```sh
cd docs/lessons-learned/scripts
./run.sh push
```

The runner creates a throwaway `--user` transient unit, runs the probe with its
loop blocked, and tears the unit down afterwards (cleanup is guaranteed by a
trap, even on error).

### Output (condensed; repeated snapshot batches trimmed)

```text
[  310.2ms] SIGKILL 73153 + BLOCK 1s
[ 1311.6ms] unblocked, draining queued signals          <-- 1s later, loop resumes
[ 1312.3ms] PropsChanged changed={Result:signal, ExecMainStatus:9, NRestarts:0}
[ 1313.3ms] PropsChanged changed={ActiveState:failed, SubState:failed}      <-- preserved
[ 1314.2ms] PropsChanged changed={Result:signal, ExecMainStatus:9, NRestarts:1}  <-- counted
[ 1313.9ms] PropsChanged changed={ActiveState:activating, SubState:auto-restart}
[ 1316.3ms] PropsChanged changed={ActiveState:active, SubState:running}      <-- recovered
[ 1316.3ms] JobRemoved result=done
```

Match the **assertions**, not the exact bytes: timestamps shift per run and the
live stream carries more repeated `Result`/snapshot batches than shown here. The
proof is that after a 1-second blackout the whole cycle arrived intact and in
order — `failed`, `Result=signal`, `ExecMainStatus=9`, `NRestarts` 0 -> 1,
recovery, and the authoritative `JobRemoved result=done`.

### What it proves

- A blocked loop delays but does not drop. Push is reliable.
- `JobRemoved` is a discrete per-job event and is never coalesced — the
    authoritative spine for "what happened." `PropertiesChanged` *can* coalesce
    several changes within one systemd emit cycle, so it is enrichment, not spine.

## Experiment B - react to ActiveState, enrich from the cache

**Question:** reacting only when `ActiveState` transitions, and then reading
`SubState`/`Result`/`ExecMainStatus` **from the proxy cache** — is that read
race-free, or does the auto-restart overwrite the cache before the handler looks
(the Experiment-A race through the back door)?

**Wire-protocol fact first:** systemd emits only `PropertiesChanged` on the bus;
there is **no per-property signal**. "React to `ActiveState`" is a *client-side*
notion — a cached proxy (`Gio.DBusProxy`, or GLib's generated typed Unit proxy
with `notify::active-state`) consumes the raw traffic and gives clean per-
property notifications. The bytes still arrive; only your *logic* runs selectively.

**Method** ([`scripts/cache-probe.py`](./scripts/cache-probe.py)): one cached
proxy per interface (`...Unit`, `...Service`) on the same unit object; connect
`g-properties-changed` **only on the Unit proxy**; react only to batches carrying
`ActiveState`; on `failed`, read the companions **from the Service proxy cache —
no fresh `Get`**.

### Reproduce

```sh
cd docs/lessons-learned/scripts
./run.sh cache
```

### Output (loop blocked 1s)

```text
[  301.1ms] SIGKILL 73233 + BLOCK 1s
[ 1302.9ms] unblocked, draining
[ 1304.5ms] ActiveState -> deactivating SubState=stop-sigterm
[ 1305.0ms] ActiveState -> failed       SubState=failed  | FROM CACHE: Result=signal ExecMainStatus=9 NRestarts=0
[ 1305.5ms] ActiveState -> activating   SubState=auto-restart
[ 1306.7ms] ActiveState -> inactive     SubState=dead
[ 1307.1ms] ActiveState -> active       SubState=running

[stats] Unit change batches total=8, carried ActiveState=8 (logic ran 8x instead of 8x)
```

### What it proves

- **Cache reads are race-free IF done inside the change handler.**
    `Gio.DBusProxy` applies each queued `PropertiesChanged` to its cache and
    *then* emits `g-properties-changed`, one batch at a time in queue order. When
    the handler runs for the `failed` batch, the cache reflects the world *as of
    that batch* — `Result=signal`, `ExecMainStatus=9`, not the later `success`/`0`.
    The Experiment-A race only happened because that probe did a fresh `Get()`,
    which jumps to "now."
- **The efficiency win comes from splitting by interface, not from filtering
    `ActiveState`.** systemd bundled `ActiveState` into every *Unit* batch, so the
    "only if `ActiveState` present" filter dropped nothing (8 of 8). The real
    reduction is that the handler is connected only to the **Unit** proxy, so
    Service-interface changes (`Result`/`ExecMainStatus`/`NRestarts`) never run
    logic — they are read from cache on demand.
- **Neither the presence check nor `notify::active-state` is edge-triggered.**
    systemd re-emits `ActiveState=activating` several times during the restart
    churn (hence the repeated lines), each time *including* the property at an
    unchanged value. The generated gdbus-codegen proxy does **not** value-gate: its
    `g-properties-changed` override calls `g_object_notify` for **every property
    present** in the payload, with no old-vs-new comparison —

    ```c
    while (g_variant_iter_next (iter, "{&sv}", &key, NULL)) {
        info = ... lookup ...;
        if (info != NULL)
            g_object_notify (G_OBJECT (proxy), info->hyphen_name);  /* no value compare */
    }
    ```

    So a typed proxy's `notify::active-state` fires just as many times as the
    presence check — the wire payload is identical regardless of language. True
    edge detection requires the client to keep the last value and compare it; the
    precision comes from that guard, not from the proxy type or the signal.

### Addendum — coalescing never hid a `failed` edge

`PropertiesChanged` *can* coalesce several changes within one systemd emit cycle,
which raised a fear: could a very fast failure+restart collapse
`active → failed → activating → active` into one batch, so a subscriber watching
`ActiveState` never sees `failed`? A tight-crash-loop probe (`RestartSec=0`, 15
back-to-back `SIGKILL`s) answered it:

```text
ActiveState: deactivating failed activating activating activating inactive active active   (×15)
kills=15   'failed' edges observed=15   max NRestarts=15
VERDICT: NO GAP
```

Every restart carried a distinct, visible `failed` edge even at the fastest restart
systemd allows. So the value-gated `ActiveState` transition is a sufficient spine on
its own; it does not need `JobRemoved` as a never-coalesced backstop. (This is why
step-3 drops `JobRemoved` — ADR 0006.) Note the `inactive` in that
sequence: `ActiveState=inactive` occurs *mid-auto-restart*, so it is not a
clean-stop signal — clean-stop detection must use `UnitRemoved` instead (ADR 0007).

## Discipline for the Supervisor

- Call the Manager's `Subscribe()` before issuing any work (ADR 0004).
- Use the value-gated `ActiveState` transition as the spine (it is never coalesced
    away — see the addendum). `JobRemoved` is the natural spine only when *you*
    issued the job (step-2); a pure observer does not need it.
- Read `Result`/`ExecMainStatus`/`NRestarts` from the `PropertiesChanged`
    payload, or from the proxy cache **inside** the ordered change callback.
- Never re-`Get` a property after being woken — that read races the restart.
- Value-gate `ActiveState`: keep the last seen value per unit and act only on a
    real transition. The payload (and `notify::`) fire on every announcement, not
    only on change.

## Environment

Verified on systemd 252 (252.39-1~deb12u2), user/session instance, with
`python3` + PyGObject (`gi`). The scripts use only the per-user systemd instance
and a throwaway transient unit, so they never touch system services.
