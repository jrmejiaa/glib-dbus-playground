# 001 - D-Bus Concepts: Bus, Name, Object, Interface, Method, Signal

A protocol-level overview of D-Bus, independent of any language binding. This is
the vocabulary everything else builds on; GLib's representation of these ideas is
covered in lesson 003.

## What D-Bus is

D-Bus is an inter-process communication (IPC) system. Processes connect to a
**bus** (a message router) and exchange messages — method calls, replies, and
signals — with other processes on the same bus.

There are two standard buses:

- **System bus**: one per machine, for system services (systemd, NetworkManager).
    Access is privileged and often gated by polkit.
- **Session bus**: one per login session, for the user's own applications.

A process opens a **connection** to a bus. On connecting it is assigned a unique
connection name like `:1.5` (the colon marks it unique and bus-assigned).

## The addressing hierarchy

To call something, a client must name four things, from coarse to fine:

1. **Well-known name** — a reverse-DNS service identifier, e.g.
    `org.freedesktop.systemd1`. It is a stable alias for whichever connection
    currently owns it, so clients do not need to know the volatile `:1.5` name.
    One service owns one (or more) well-known names.
2. **Object path** — a filesystem-like path identifying one object exported by
    that service, e.g. `/org/freedesktop/systemd1`. A service can export many
    objects (systemd exports one per unit, e.g.
    `/org/freedesktop/systemd1/unit/poc_2ddummy_2eservice`).
3. **Interface** — a named contract grouping related methods, signals, and
    properties, e.g. `org.freedesktop.systemd1.Manager`. One object can implement
    several interfaces (its own, plus standard ones like
    `org.freedesktop.DBus.Properties`).
4. **Member** — the specific method, signal, or property name, e.g. `StartUnit`.

Read it as: *name → object → interface → member*. For example, call `StartUnit`
on interface `org.freedesktop.systemd1.Manager` of object
`/org/freedesktop/systemd1` owned by `org.freedesktop.systemd1`.

## Members: methods, signals, properties

- **Method**: a request/response call. The client calls it and (usually) gets a
    reply or an error. Example: `StartUnit(name, mode) -> job object path`.
- **Signal**: a one-way broadcast emitted by the service; clients that have a
    matching subscription receive it. No reply. Example: `JobRemoved(id, job,
    unit, result)`. Signals are how a service pushes events to clients.
- **Property**: a named value on an interface, read/written through the standard
    `org.freedesktop.DBus.Properties` interface (`Get`/`Set`/`GetAll`), with
    changes announced via the `PropertiesChanged` signal. Example: the Unit
    interface's `ActiveState`.

## Type signatures

Method and signal arguments are typed with a compact signature string: `s`
string, `u` uint32, `t` uint64, `o` object path, `b` boolean, `a` array, `(...)`
struct. For instance `ListUnits` returns `a(ssssssouso)` — an array of 10-field
structs.

Important limitation: a signature describes the **wire type only**, never the
**allowed values**. A `mode` argument of type `s` does not say which strings are
legal (`replace`, `fail`, `isolate`, ...). Those value constraints are a
documentation contract from the service, not something introspection encodes.

## Introspection

A D-Bus object can describe itself: calling the standard
`org.freedesktop.DBus.Introspectable.Introspect` method returns an XML document
listing its interfaces, methods, signals, and properties. This is the machine-
readable contract that tools (lesson 002) and code generators consume. It
contains types and member names, but — per the limitation above — not value
constraints or whether signals require a subscription (lesson 005).
