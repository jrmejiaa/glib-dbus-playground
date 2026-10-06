# Glib DBus Playground for Systemd

A proof-of-concept that drives the systemd Manager over D-Bus from a C++ client
built on the GLib `gdbus-codegen` generated proxy, exercising unit lifecycle
(start/stop/restart) and listing against an isolated dummy service.

## Language

**Manager**:
The systemd D-Bus object (`org.freedesktop.systemd1` at
`/org/freedesktop/systemd1`) that owns global operations like starting units and
listing them. The single object the client talks to.
_Avoid_: systemd service, daemon (those mean other things here).

**Unit**:
A systemd-managed entity (here, a `.service`). Each unit is its own D-Bus
object exposing `ActiveState`/`SubState`, distinct from the Manager.
_Avoid_: service (ambiguous with "D-Bus service"), process.

**Proxy**:
The client-side generated GObject (`...ManagerProxy`) that stands in for the
remote Manager. The client always uses a Proxy.
_Avoid_: stub, client object.

**Skeleton**:
The server-side generated type used to *export* an interface. systemd owns the
skeleton; the PoC never uses one.
_Avoid_: server stub.

**Job**:
systemd's unit of work created when a lifecycle action is requested
(start/stop/restart). Identified by an object path; completes asynchronously.
_Avoid_: task, request.

**JobRemoved**:
The Manager signal fired when a Job leaves the queue, carrying its `result`
(`done`, `failed`, `canceled`, `timeout`, ...). The authoritative "my action
completed" signal.
_Avoid_: job done event, completion callback.

**Readiness**:
The point at which a unit has finished initializing and can do its work. For
`Type=notify`/`dbus` units this coincides with Job completion; for
`simple`/`exec` it does not exist as a distinct event.
_Avoid_: started, active (those are weaker claims).

**Readiness protocol**:
The mechanism by which a service tells systemd it is ready: `sd_notify(READY=1)`
for `Type=notify`, bus-name acquisition for `Type=dbus`.
_Avoid_: handshake, ready signal.

**Liveness**:
Whether a unit's process is still alive/healthy *after* start, independent of
any action the client issued. Observed via the Unit object, not the Manager.
_Avoid_: health, status, uptime.

**Dummy service**:
`poc-dummy.service`, the isolated `Type=notify` unit the PoC starts/stops/
restarts so it never touches real system services.
_Avoid_: test service, fake unit.

**Introspect XML**:
The hand-authored D-Bus interface description (the `<node>`/`<interface>` IDL)
that `gdbus-codegen` consumes to emit bindings. Lives in each sub-project's
`introspect/` directory. It is the *source*, not an artifact.
_Avoid_: api, schema, spec.

**Generated code**:
The `gdbus-codegen` output (`.c`/`.h`) derived from an Introspect XML. Lives in
each sub-project's `generated/` directory and is compiled as its own C static
library that C++ targets link.
_Avoid_: proxy code, bindings (too narrow — the output carries Proxy *and*
Skeleton).

**Supervisor**:
The long-running step-3 process that watches a set of daemons and narrates the
complete truth of the fleet: what systemd did to each unit and what application
state each daemon reports. It consumes two interfaces at once: the systemd
Manager/Unit (to observe lifecycle) and each daemon's own interface (to read its
State). systemd owns failure-revival; the Supervisor owns clean-stop revival
(ADR 0007), observes, and coordinates.
_Avoid_: lifecycle-manager, monitor, systemd-client.

**Revive / Revival**:
Bringing a dead or stopped daemon back to running. Split by cause in step-3:
**systemd** revives unclean failures (`Restart=on-failure`); the **Supervisor**
revives clean stops (`exit(0)`/`SIGTERM`/`systemctl stop`), triggered by
`UnitRemoved` and issued as `StartUnit(name)` (ADR 0007).
_Avoid_: restart (ambiguous with systemd's own `Restart=`), resurrect.

**Stall**:
A daemon that is still alive as a process but has stopped making progress (hung).
Distinct from death: without intervention `ActiveState` stays `active`. Detected
by systemd's Watchdog, which kills the wedged process so it surfaces as a normal
`failed` transition carrying `Result=watchdog`.
_Avoid_: hang, freeze, deadlock, wedged.

**State**:
The daemon's own application-level readiness, exposed as an `emits-change`
property on its D-Bus interface (`uint32` enum: `initializing`, `ready`,
`degraded`). The Supervisor reads it to know whether a process that systemd
reports as `active` is actually ready to coordinate — a fact systemd cannot see.
_Avoid_: status (ambiguous with `ActiveState`/`SubState`), health, Heartbeat.

**Watchdog**:
systemd's native stall mechanism: `WatchdogSec=` plus the daemon calling
`sd_notify(WATCHDOG=1)` every `WatchdogSec/2`. If a ping is missed, systemd kills
and (with `Restart=on-failure`) restarts the unit, setting `Result=watchdog`. This
is the chosen stall-detection mechanism; the Supervisor observes its effect via
the event stream rather than detecting stalls itself.
_Avoid_: using "watchdog" loosely for an app-level liveness ping to the Supervisor.

**Event stream**:
systemd's D-Bus signals (`UnitNew`/`UnitRemoved` on the Manager, `PropertiesChanged`
per unit) that the Supervisor consumes as its source of truth. Delivered in order
and queued on the connection, so a busy loop delays but never drops them. In step-3
the spine is the value-gated `ActiveState` transition (a coalescing test showed it
is never hidden, 15/15 at `RestartSec=0`); `UnitNew`/`UnitRemoved` drive proxy
attach/detach and clean-stop revival. `JobRemoved` is a step-2 concept, not used here
(ADR 0006).
_Avoid_: polling, property fetch (the rejected alternative).
