# 0005 - Monitor liveness/death per-unit, not via the Manager

Detecting that a unit has died (or otherwise changed state on its own) will be
done by watching the **Unit object's** `PropertiesChanged` signal for
`ActiveState`/`SubState`, not by listening to any Manager-level signal. This is
scoped as future work and is not yet implemented.

We record this because it is counterintuitive: the Manager has no "a unit died"
event. `JobRemoved` only fires for *jobs*, and a process dying on its own creates
no job, so Manager signals cannot report unprompted death. Liveness is a per-unit
lifecycle fact and lives on the per-unit object — meaning one D-Bus object (and
subscription) per unit watched.

## Consequences

- Monitoring N units means N unit-object subscriptions; obtain each path via
  `GetUnit`/`LoadUnit`.
- `ActiveState=failed`/`inactive` detects *process exit*, not *hangs*: an alive-
  but-wedged process needs `WatchdogSec` + `sd_notify(WATCHDOG=1)` or an
  app-level health check. Plain `ActiveState` will not catch it.
- Implementation estimate: ~2.5h reading `ActiveState` generically via the
  `org.freedesktop.DBus.Properties` interface (no second `gdbus-codegen`), or
  ~4h generating a typed Unit proxy. The introspected XML currently contains
  only the Manager interface, so the typed route requires a second codegen.
