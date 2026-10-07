# 002 - Tooling: gdbus, gdbus-codegen, busctl, and Monitors

How to explore a live D-Bus service and turn its interface into C code. All
commands below were run against the systemd user instance on the project's
machine (GLib `gdbus`, `gdbus-codegen`, and systemd 252 `busctl`).

## Exploring a live service

### gdbus introspect

Dump an object's interfaces, methods, signals, and properties. Add `--xml` to
get the machine-readable form that feeds the code generator.

```sh
# Human-readable:
gdbus introspect --session --dest org.freedesktop.systemd1 \
    --object-path /org/freedesktop/systemd1

# XML (what you save to feed gdbus-codegen):
gdbus introspect --session --dest org.freedesktop.systemd1 \
    --object-path /org/freedesktop/systemd1 --xml > systemd-dbus.xml
```

Use `--system` instead of `--session` for the system bus.

### busctl (systemd)

`busctl` is systemd's D-Bus tool and is especially good at listing and reading
properties. It shows each property's type, current value, and whether it emits
change signals (`emits-change`):

```sh
busctl --user introspect org.freedesktop.systemd1 \
    /org/freedesktop/systemd1/unit/poc_2ddummy_2eservice
# ...
# .ActiveState   property  s  "active"   emits-change
# .SubState      property  s  "running"  emits-change
```

(`--user` for the user instance; drop it for the system bus.)

### Calling a method by hand

Useful to prove behavior (e.g. that a bad enum value is rejected at runtime):

```sh
gdbus call --session --dest org.freedesktop.systemd1 \
    --object-path /org/freedesktop/systemd1 \
    --method org.freedesktop.systemd1.Manager.GetUnit "poc-dummy.service"
```

### Watching signals: monitors

```sh
# GLib monitor, scoped to one service:
gdbus monitor --session --dest org.freedesktop.systemd1

# Lower-level, sees all bus traffic:
dbus-monitor --session
```

Caveat (see lesson 005): a monitor puts the bus into an eavesdropping mode and
may show signals a *normal* client would not receive without subscribing. Do not
use "I saw it in the monitor" as proof that your client will receive it.

## Generating C bindings: gdbus-codegen

`gdbus-codegen` turns the introspection XML into a C client/server binding. The
two naming flags determine the generated symbol names:

- `--interface-prefix` strips a common prefix from interface names before
    converting them to C identifiers.
- `--c-namespace` is the CamelCase prefix prepended to every generated type.

The project's files were generated such that interface
`org.freedesktop.systemd1.Manager`, with `--interface-prefix org.freedesktop.`
and `--c-namespace Gdbus`, becomes the C type
`GDbusOrgFreedesktopSystemd1Manager`.

```sh
# Produce both header and body in one go (OUTFILES is a prefix):
gdbus-codegen \
    --interface-prefix org.freedesktop. \
    --c-namespace Gdbus \
    --generate-c-code gdbus-systemd-manager \
    systemd-dbus.xml
# -> gdbus-systemd-manager.h and gdbus-systemd-manager.c
```

`--header` / `--body` emit only one file each if you prefer them separate.

## Naming rules the generator applies

The same D-Bus name appears in three spellings, each required by a different
layer. Knowing the mapping lets you predict every generated name:

| Concept        | D-Bus (XML)   | GObject signal   | C identifier    |
|----------------|---------------|------------------|-----------------|
| Signal         | `JobRemoved`  | `job-removed`    | `job_removed`   |
| Method         | `StartUnit`   | n/a              | `start_unit`    |

- **GObject signal names**: lowercase, words separated by `-`. Derived by
    lowercasing the PascalCase D-Bus name and inserting `-` at word boundaries
    (`JobRemoved` -> `job-removed`). This is the string you pass to
    `g_signal_connect`. (`-` and `_` are interchangeable in GObject signal names,
    but the hyphen form is canonical.)
- **C identifiers**: snake_case, since C has no `-`. Method call wrappers are
    `<namespace>_<interface>_call_<method>()`, e.g.
    `gdbus_org_freedesktop_systemd1_manager_call_start_unit()`.

A misspelled signal name is not a compile error — it is a runtime GLib warning
and a handler that never fires, because the name is matched as a plain string.

## Compiling the generated code

The output is C. Build it as its own static library with the C compiler and link
it into C++ targets; its header is wrapped in `extern "C"`, so including it from
C++ is safe. See the project `CMakeLists.txt` and the C/C++ steering file.

## What about libsystemd (libsystemd-dev)?

A recurring question: does `libsystemd` already provide the D-Bus wiring and, say,
an `ActiveState` enum to compare against? Checked against the installed headers
(`/usr/include/systemd/`, systemd 252):

- **No public unit-state enum exists.** Grepping every public header for
    `ActiveState` / `UnitActiveState` returns nothing. `ActiveState` is a D-Bus
    string (`s`) on the wire, so *every* client — `sd-bus` or GLib's generated
    proxy — receives it as a string. (systemd's source has an internal
    `UnitActiveState` enum with `unit_active_state_from_string()`, but it is private
    and unexported.) So you define your own `enum class` and convert the string at
    the boundary; nothing in the dev package replaces that.
- **What it does provide:** `sd-bus` (a D-Bus client stack, a lower-level
    *alternative* to GLib + `gdbus-codegen` — no typed per-interface proxies),
    `sd-event` (an event loop, an alternative to `GMainLoop`), and `sd-daemon`
    (`sd_notify()`, `sd_notifyf()`, `sd_watchdog_enabled()` — the daemon-side
    readiness/watchdog helpers).

For this project the only overlap worth noting is `sd-daemon`: its `sd_notify` /
`sd_watchdog_enabled` are exactly what the dummy daemon hand-rolls over the
`NOTIFY_SOCKET`/`WATCHDOG_USEC` environment variables. The hand-rolled version is a
deliberate choice to keep the daemon free of a `libsystemd` link dependency; adopting
`sd-daemon` would trade that dependency for less boilerplate. Nothing in `libsystemd`
helps the Supervisor's GDBus wiring or gives you a state enum.
