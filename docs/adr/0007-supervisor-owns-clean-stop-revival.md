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
