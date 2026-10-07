# 0007 - The Supervisor owns clean-stop revival; systemd owns failure revival

Revival is split by cause. **systemd** restarts a unit that ends **uncleanly**
(crash, watchdog stall, non-zero `exit(N)`, timeout) via `Restart=on-failure`.
The **Supervisor** restarts a unit that ends **cleanly** (`exit(0)`, `SIGTERM`,
`systemctl stop`) by issuing `StartUnit(name)` on the Manager. `Restart=on-failure`
deliberately does *not* cover clean stops, and that gap is the Supervisor's
responsibility: the fleet policy is "these daemons must stay running," and the
Supervisor is the authority on it.

The Supervisor's revive trigger is the Manager's **`UnitRemoved`** signal for a
tracked unit — not `ActiveState=inactive`.

We record the trigger choice because `inactive` looks like the obvious signal and
is wrong. A probe (`RestartSec=0`, repeated `SIGKILL`) showed every auto-restart
cycle passes *through* `inactive`:

```text
deactivating  failed  activating  activating  activating  inactive  active  active
```

So reviving on `ActiveState=inactive` would fire `StartUnit` in the middle of
systemd's own auto-restart, fighting it. `UnitRemoved`, by contrast, means "gone
and staying gone": an auto-restarting unit stays **loaded** (pending restart) and
a terminally `failed` unit stays **loaded** until `reset-failed`, so neither emits
`UnitRemoved`. Only a cleanly stopped, unreferenced unit is garbage-collected and
emits it. That makes `UnitRemoved` a precise discriminator that needs no debounce
to tell clean-stop apart from failure.

## Considered options

- **`Restart=always`** (systemd revives clean stops too). Rejected: it erases the
  Supervisor's lifecycle role and collapses step-3 back into a pure observer. The
  deliberate `on-failure` gap is what gives the Supervisor something to own.
- **Revive on `ActiveState=inactive`** (from the per-unit `UnitMonitor`). Rejected:
  `inactive` also occurs mid-auto-restart (see above), so it cannot distinguish a
  clean stop from systemd's own restart without a debounce.
- **Revive on `UnitRemoved`** (chosen). A clean, undebounced "cleanly gone" signal
  that fires promptly (~10 ms after `systemctl stop`, measured on systemd 252) and
  is not suppressed by holding an open proxy on the unit object.

## Consequences

- Revival must be **idempotent**. A single clean stop can emit `UnitRemoved`
  (and `UnitNew`) more than once in a burst during teardown — observed as
  `UnitRemoved → UnitNew → UnitRemoved → ...` within ~0.1 ms on a transient test
  unit. `StartUnit` on an already-starting unit joins the existing job, so
  repeated triggers are harmless; attach-on-`UnitNew` must likewise build a
  `UnitMonitor` only if none exists for that name. The flap should be re-checked
  against the real template units (`poc-dummy@.service`) during implementation.
- The two revival owners never collide: a failure never reaches a clean
  `inactive`/GC state (it goes `failed → activating`), and a clean stop never
  triggers `Restart=on-failure`. So exactly one of {systemd, Supervisor} acts on
  any given end-of-life.
- `start-limit-hit` units rest in `failed` (still loaded, no `UnitRemoved`), so the
  Supervisor does **not** revive them — it correctly observes the give-up instead
  of fighting the start limit.
- The Supervisor must guard against reviving during its own shutdown (do not react
  to `UnitRemoved` once tearing down).

## Revision 2026-10-07 — debounced trigger (refines the "no debounce" stance above)

The original decision above revived on `UnitRemoved` **undebounced**, relying on
idempotency to absorb the teardown burst. Further measurement (lesson 007) refined
the trigger mechanism. The core decision is unchanged — the Supervisor revives on
`UnitRemoved`, systemd owns failure-revival — but the trigger is now **debounced**.
This section supersedes the "needs no debounce" / "undebounced" wording above.

Three facts, now confirmed (lesson 007), drove the change:

- The `UnitRemoved → UnitNew → …` burst is a **`systemctl` CLI artifact**: a D-Bus
  `Manager.StopUnit` emits exactly one `UnitRemoved`. (This resolves the earlier
  "re-check the flap against real template units" caveat — the flap is the tool,
  not the unit.)
- A unit's **object path is stable** (deterministic from the name), identical across
  reload and within a burst — so a `UnitNew` in a debounce window means "same unit,
  still here," needing no path rebind.
- `UnitRemoved` **only ever means "cleanly gone"**: a crash + auto-restart emits no
  `UnitNew`/`UnitRemoved` at all.

Combined with the ~2.5 ms cost of building a `UnitMonitor`'s proxies (lesson 008),
acting on each raw `UnitRemoved` would needlessly tear down and rebuild proxies
during the `systemctl` burst. Instead the Supervisor runs a small state machine per
tracked name, keyed off a short (~50 ms) timer owned by the Supervisor:

```text
ABSENT          no monitor
ACTIVE          monitor exists, proxies bound
PENDING_REMOVAL monitor exists, removal timer armed

UnitNew  in ABSENT             -> build monitor (sync)            -> ACTIVE
UnitNew  in ACTIVE             -> no-op (path stable)             -> ACTIVE
UnitNew  in PENDING_REMOVAL    -> cancel timer (it is back)       -> ACTIVE
UnitRemoved in ACTIVE          -> arm ~50 ms timer                -> PENDING_REMOVAL
UnitRemoved in PENDING_REMOVAL -> re-arm timer                    -> PENDING_REMOVAL
timer fires in PENDING_REMOVAL -> tear down monitor; StartUnit    -> ABSENT
```

The timer runs on the main loop, so nothing blocks. The `systemctl` burst
(`R,N,R,N,R`) walks `ACTIVE→PENDING→ACTIVE→PENDING→ACTIVE→PENDING→(timer)→ABSENT`,
yielding **one** teardown and **one** revive with **zero** intermediate builds. A
genuine `exit(0)` (single `UnitRemoved`) walks `ACTIVE→PENDING→(timer)→ABSENT`
identically, so clean stop and clean self-exit are handled the same way. Because the
build happens only on a genuine appearance, the ~2.5 ms sync construction is never
multiplied by the burst, so plain sync construction in the `UnitNew` handler is
acceptable at PoC scale (lesson 008).

The window (~50 ms) is a `constexpr` constant: ~100× the measured sub-millisecond
burst span, yet imperceptible as revival latency. The idempotency and
shutdown-guard consequences above still hold.

### The debounce is correctness-critical, not just efficiency (lesson 009)

The stronger reason for the debounce — beyond avoiding needless build/teardown — is
that acting on a raw `UnitRemoved` by tearing down and then rebuilding on the burst's
`UnitNew` **resurrects the dead unit**. Building a `FLAGS_NONE` proxy issues a
`GetAll`, which forces systemd to load the inactive unit (`UnitNew`) and then GC it
again (`UnitRemoved`); the Supervisor reacts by rebuilding, and the two feed an
**infinite loop** (lesson 009, proven in isolation: one `GetAll` on a confirmed-gone
unit emits a `UnitNew`/`UnitRemoved` pair). A passive observer that never calls
`GetAll` settles; the proxy build is the engine of the loop.

The debounce stops this because the monitor is **not torn down until the timer
fires**: a `UnitNew` during the burst finds the monitor present and does not rebuild,
so no `GetAll` is issued and nothing is resurrected. Crucially, **this correctness
property is independent of the window value** — it holds for any positive window,
because what breaks the loop is "do not rebuild while a removal is pending," not the
particular delay. The window only tunes revival latency.

As defense-in-depth, `G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES` removes the
construction `GetAll` entirely (no resurrection, measured), at the cost of the
initial property snapshot (the cache then fills from the first `PropertiesChanged`).
The debounce is still what guarantees the proxy is only built on a genuinely-present
unit, so the retained `FLAGS_NONE` `GetAll` is safe.
