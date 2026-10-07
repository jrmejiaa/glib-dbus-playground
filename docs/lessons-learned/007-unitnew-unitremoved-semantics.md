# 007 - UnitNew/UnitRemoved semantics: the burst, the stable path, and "cleanly gone"

## The lesson

The Manager's `UnitNew`/`UnitRemoved` signals are the backbone of observing when a
unit's D-Bus object appears and disappears, but three non-obvious facts decide how
you must consume them. All three are reproducible with the scripts in
[`scripts/`](./scripts); each creates its own throwaway `--user` unit and cleans up.

1. **The `UnitRemoved`/`UnitNew` burst on a clean stop is a `systemctl` CLI 
     artifact — not systemd.** One `systemctl stop` emits a flurry
    (`UnitRemoved → UnitNew → UnitRemoved → …`), but the same stop issued over
    D-Bus (`Manager.StopUnit`) emits **exactly one** `UnitRemoved`.
2. **A unit's object path is stable.** It is derived deterministically from the
    unit name (`/org/freedesktop/systemd1/unit/<escaped-name>`), identical across
    unload/reload and for every event within a burst.
3. **`UnitRemoved` means "cleanly gone," never "crashed."** A crash +
    `Restart=on-failure` restart emits **no** `UnitNew`/`UnitRemoved` at all — the
    unit stays loaded the whole time.

Together these say: treat a `UnitRemoved` as a *debounced* signal of clean
departure. Wait a few tens of ms for any `systemctl`-driven burst to settle; if the
unit is still gone, it genuinely left cleanly (clean stop or `exit(0)`) and you can
act (e.g. revive). Because the path is stable and a crash never emits these signals,
the debounce is safe and unambiguous. This is the empirical basis for the
clean-stop revival design in ADR 0007.

## Experiment 1 - the burst is a `systemctl` artifact

[`scripts/burst-probe.py`](./scripts/burst-probe.py) subscribes passively (no proxy
held, no `GetUnit`) and stops the same unit two ways.

```sh
python3 scripts/burst-probe.py systemctl   # stop via the CLI tool
python3 scripts/burst-probe.py dbus        # stop via Manager.StopUnit
```

Observed (stop window only):

```text
via systemctl :  UnitRemoved → UnitNew → UnitRemoved → UnitNew → UnitRemoved   (3 + 2)
via dbus      :  UnitRemoved                                                   (1)
```

The CLI tool loads the unit to operate on it and report status, and that transient
reference churns it load→unload a few times. systemd's own stop+GC emits a single
`UnitRemoved`. So anything the Supervisor does over D-Bus (its revival `StartUnit`,
any `StopUnit`) will not flap; the burst only appears when a human/CLI runs
`systemctl stop` — which is exactly the clean-stop fault in the demo, so the
Supervisor must still tolerate it there.

## Experiment 2 - the object path is stable

[`scripts/path-probe.py`](./scripts/path-probe.py) reads the path before and after a
stop/start cycle, and logs the path on every event within a burst.

```sh
python3 scripts/path-probe.py
```

Observed:

```text
(A) path before = /org/freedesktop/systemd1/unit/poc_2dpath_2eservice
(A) path after  = /org/freedesktop/systemd1/unit/poc_2dpath_2eservice
(A) STABLE across reload? True
(B) distinct paths in burst = 1   (all events carry the same path)
```

The path is a deterministic escaping of the unit name, so it does not change across
reload or within a burst. A `UnitNew` arriving during a debounce window therefore
means "same unit, same path, still here" — cancelling a pending teardown is safe,
with no path rebind needed.

## Experiment 3 - `UnitRemoved` means "cleanly gone"

[`scripts/crash-churn-probe.py`](./scripts/crash-churn-probe.py) `SIGKILL`s a
`Restart=on-failure` unit repeatedly and counts `UnitNew`/`UnitRemoved`.

```sh
python3 scripts/crash-churn-probe.py
```

Observed:

```text
kills issued = 5
UnitNew during crashes     = 0
UnitRemoved during crashes = 0
VERDICT: no churn -> UnitRemoved means cleanly gone
```

A crashing unit stays **loaded** through `failed → activating → active`, so no
`UnitNew`/`UnitRemoved` fire. Therefore `UnitRemoved` is never a crash — it is
clean departure (stop / `exit(0)` / unload). Reviving on a settled `UnitRemoved`
can never collide with systemd's own auto-restart.

## Design consequence

The Supervisor keys a small state machine per tracked unit name (see ADR 0007):

```text
UnitRemoved -> arm a ~50 ms timer (do not act yet)
UnitNew within the window -> cancel the timer (it is back / never really left)
timer fires -> the unit is settledly gone: tear down the monitor, then revive
```

This coalesces the `systemctl` burst to a single decision, treats a clean stop and a
daemon `exit(0)` identically, and — because the path is stable and crashes emit
nothing here — never rebuilds proxies needlessly nor fights systemd's auto-restart.

## Environment

Verified on systemd 252 (252.39-1~deb12u2), user/session instance, `python3` +
PyGObject. The scripts use only the per-user systemd instance and throwaway
transient units, so they never touch system services.
