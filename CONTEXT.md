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
