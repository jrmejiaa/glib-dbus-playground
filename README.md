# GLib D-Bus Playground

A learning journey into D-Bus on Linux with GLib's `gdbus-codegen`, from the
point of view of someone meeting D-Bus for the first time. The repo is a
playground: each step is a self-contained sub-project with its own build, and
this page tells the story that connects them.

## The journey

The goal was always to drive **systemd** over D-Bus. But systemd is a large,
real service, so I warmed up on a tiny toy interface first, courtesy of 
Aleksander Morgado in his presentation of [Introduction to D-Bus](https://aleksander.es/data/GNOMEASIA2014%20-%20Introduction%20to%20DBus.pdf).
then moved to the real thing. The two steps live side by side:

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

The contrast between the two steps is the lesson: in step 1 I own both ends of
the wire; in step 2 I own only the client and talk to software I did not write.

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
│   ├── service/            exports the interface  (implemented)
│   ├── client/             calls the interface    (implemented)
│   └── CMakeLists.txt      standalone build
├── systemd/             Step 2: driving real systemd over D-Bus
│   ├── introspect/         the systemd Manager interface description (the source)
│   ├── generated/          gdbus-codegen output: the Manager Proxy (build-time, gitignored)
│   ├── client/             the D-Bus client + SystemdManager wrapper
│   ├── daemon/             the dummy daemon + its .service unit template
│   └── CMakeLists.txt      standalone build (owns the install / unit logic)
├── cmake/
│   └── GdbusCodegen.cmake  shared helper: run gdbus-codegen at build time
├── docs/
│   ├── adr/                architecture decision records
│   └── lessons-learned/    reusable D-Bus / GLib notes gathered along the way
├── CONTEXT.md              glossary of the domain language used across the repo
└── CMakeLists.txt          thin aggregator: builds both steps
```

Two terms recur throughout and have precise meanings (see `CONTEXT.md`):
**Introspect XML** is the hand-written interface description that lives in each
step's `introspect/` directory and is the *source*; **Generated code** is the
`gdbus-codegen` output derived from it, kept in each step's `generated/`
directory and compiled as its own C static library.

## Building

Each step is a fully standalone CMake project and can be built on its own:

```sh
cmake -S alarm   -B build/alarm   && cmake --build build/alarm
cmake -S systemd -B build/systemd && cmake --build build/systemd
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

## Where things are generated from

The `generated/` code is produced at build time by `gdbus-codegen` from the
`introspect/` XML (into the source tree, gitignored, for dev visibility) — see
[ADR 0002](docs/adr/0002-generate-bindings-at-build-time.md).

## Status

- **Step 1 (alarm)** is implemented on both sides: the service responds to the
  `Configure` call and uses a GLib timeout to fire the alarm (the `Beeeeeeeeep`
  signal), and the client configures the alarm and waits for that signal.
- **Step 2 (systemd)** is implemented: client, dummy daemon, unit install.

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
