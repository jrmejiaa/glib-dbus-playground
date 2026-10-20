# GLib D-Bus Playground

A learning journey into D-Bus on Linux with GLib's `gdbus-codegen`, from the
point of view of someone meeting D-Bus for the first time. The repo is a
playground: each step is a self-contained sub-project with its own build, and
this page tells the story that connects them.

## The journey

The goal was always to drive **systemd** over D-Bus. But systemd is a large,
real service, so I warmed up on a tiny toy interface first, courtesy of 
Aleksander Morgado in his presentation of [Introduction to D-Bus](https://aleksander.es/data/GNOMEASIA2014%20-%20Introduction%20to%20DBus.pdf).
then moved to the real thing, and finally combined both into a realistic
supervisor. The three steps live side by side:

1. **`alarm/`** — Step 1: the warm-up. A single, tiny D-Bus interface
    (`es.aleksander.Alarm`) that I exercise from *both* sides: a **service**
    that exports the interface (using the generated Skeleton) and a **client**
    that calls it (using the generated Proxy). This is where the
    Interface / Proxy / Skeleton model clicks. One interface, two sides, one
    shared set of generated bindings.
2. **`systemd/`** — Step 2: the real goal. A **client** that drives the actual
    systemd Manager over D-Bus (start / stop / restart / list units), plus a
    small **dummy daemon** and its unit file so the client has an isolated,
    harmless unit to manage. Here there is no hand-written service — systemd
    *is* the service; I only write the client and the thing it supervises.
3. **`supervisor/`** — Step 3: the capstone. A long-running **Supervisor** that
    watches a fleet of three dummy daemons and narrates the complete truth of
    each one. It starts them through systemd, then observes two channels at
    once: systemd's per-unit lifecycle (`UnitNew`/`UnitRemoved` and value-gated
    `ActiveState`, classifying every failure cause) and each daemon's own
    application `State` property (`org.poc.Daemon`, over `PropertiesChanged`).
    Revival is split by cause: **systemd** owns failure-revival
    (`Restart=on-failure` + watchdog), the **Supervisor** owns clean-stop
    revival (re-issuing `StartUnit` on a debounced `UnitRemoved`).

The progression is the lesson: in step 1 I own both ends of the wire; in step 2
I own only the client and talk to software I did not write; in step 3 I own the
daemon *and* the Supervisor *and* drive systemd, combining both earlier models
to observe and keep a whole fleet alive.

## Repository layout

This project layout follows the same idea as [ModemManager](https://gitlab.freedesktop.org/mobile-broadband/ModemManager)
with the difference than the generated files are saved as part of the source code
(build-time, gitignore), so that a developer could check easy the interface, skeleton
and proxy properties of the generated code easily.

```text
.
├── alarm/               Step 1: alarm example (service + client, one interface)
│   ├── introspect/         hand-authored D-Bus interface description (the source)
│   ├── generated/          gdbus-codegen output: Proxy + Skeleton (build-time, gitignored)
│   ├── service/            exports the interface
│   ├── client/             calls the interface
│   └── CMakeLists.txt      standalone build
├── systemd/             Step 2: driving real systemd over D-Bus
│   ├── introspect/         the systemd Manager interface description (the source)
│   ├── generated/          gdbus-codegen output: the Manager Proxy (build-time, gitignored)
│   ├── client/             the D-Bus client + SystemdManager wrapper
│   ├── daemon/             the dummy daemon + its .service unit template
│   └── CMakeLists.txt      standalone build (owns the install / unit logic)
├── supervisor/          Step 3: observe a daemon fleet + revive clean stops
│   ├── introspect/         three descriptions: daemon, systemd Manager, Unit+Service
│   ├── generated/          gdbus-codegen output: proxies + daemon skeleton (build-time, gitignored)
│   ├── supervisor/         the Supervisor, UnitMonitor, and DaemonStateWatcher
│   ├── daemon/             the dummy daemon (Type=notify, State property) + .service template
│   ├── fault-inject.sh     fault-injection driver for the demo
│   └── CMakeLists.txt      standalone build (install / unit template)
├── cmake/
│   └── GdbusCodegen.cmake  shared helper: run gdbus-codegen at build time
├── docs/
│   ├── adr/                architecture decision records
│   └── lessons-learned/    reusable D-Bus / GLib notes gathered along the way
├── CONTEXT.md              glossary of the domain language used across the repo
└── CMakeLists.txt          thin aggregator: builds all three steps
```

Two terms recur throughout and have precise meanings (see `CONTEXT.md`):
**Introspect XML** is the hand-written interface description that lives in each
step's `introspect/` directory and is the *source*; **Generated code** is the
`gdbus-codegen` output derived from it, kept in each step's `generated/`
directory and compiled as its own C static library.

## Building

Each step is a fully standalone CMake project and can be built on its own:

```sh
cmake -S alarm      -B build/alarm      && cmake --build build/alarm
cmake -S systemd    -B build/systemd    && cmake --build build/systemd
cmake -S supervisor -B build/supervisor && cmake --build build/supervisor
```

Or build everything at once from the root aggregator:

```sh
cmake -S . -B build && cmake --build build
```

### Running the systemd step

The systemd step installs to a rootless user prefix (`$HOME/.local`) by default,
so no `sudo` is needed. Override it with `-DCMAKE_INSTALL_PREFIX=...` at configure
time if you want a different location.

```sh
cmake --build build/systemd --target install
systemctl --user daemon-reload
./build/systemd/systemd-client
```

### Running the supervisor step

Step 3 also installs to the rootless user prefix. Install the `poc-daemon`
binary and the `poc-dummy@.service` template, reload the user manager, then run
the Supervisor — it starts `poc-dummy@{1,2,3}.service` itself on launch:

```sh
cmake --build build/supervisor --target install
systemctl --user daemon-reload
./build/supervisor/supervisor
```

With the Supervisor running in one terminal, inject faults from another and
compare its log to each fault's expected outcome. The driver covers all four
fault classes (crash, stall, app-error, clean stop):

```sh
./supervisor/fault-inject.sh demo 1       # run all four against instance 1
./supervisor/fault-inject.sh crash 2      # or one fault against one instance
```

A crash (`SIGKILL`), stall (`SIGSTOP`) or app-error (`exit(N)`) is revived by
systemd; a clean stop (`systemctl stop`) is revived by the Supervisor.

## Where things are generated from

The `generated/` code is produced at build time by `gdbus-codegen` from the
`introspect/` XML (into the source tree, gitignored, for dev visibility) — see
[ADR 0002](docs/adr/0002-generate-bindings-at-build-time.md).

## Status

- **Step 1 (alarm)** is implemented on both sides: the service responds to the
  `Configure` call and uses a GLib timeout to fire the alarm (the `Beeeeeeeeep`
  signal), and the client configures the alarm and waits for that signal.
- **Step 2 (systemd)** is implemented: client, dummy daemon, unit install.
- **Step 3 (supervisor)** is implemented: the Supervisor drives systemd to start
  the fleet, observes both the systemd lifecycle (classifying every failure
  cause) and each daemon's application `State`, and revives clean stops. Both
  observation channels are wired end-to-end; acceptance is the fault-injection
  demo rather than an automated suite.

Run step 1 — start the service, then run the client:

```sh
cmake --build build --target alarm-service alarm-client
./build/alarm/alarm-service &
./build/alarm/alarm-client
```

Or, instead of the client, drive the service by hand with `gdbus`:

```sh
gdbus call --session --dest es.aleksander.Alarm \
    --object-path /es/aleksander/Alarm \
    --method es.aleksander.Alarm.Configure 3
```
