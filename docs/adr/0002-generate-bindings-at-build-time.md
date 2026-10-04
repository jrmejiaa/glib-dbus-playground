# 0002 - Generate D-Bus bindings at build time from the Introspect XML

Each step keeps its hand-authored D-Bus interface description under
`introspect/` (the *source*) and the `gdbus-codegen` output under `generated/`
(the *artifact*). CMake runs `gdbus-codegen` at build time via
`add_custom_command`, consuming `introspect/*.xml` and producing the
`generated/*.c`/`.h`, compiled into the step's C static library (`alarm_iface`,
`systemd_manager`). The invocation is factored into a shared helper,
`cmake/GdbusCodegen.cmake` (a `gdbus_generate()` function each sub-project
includes via `CMAKE_MODULE_PATH`); it locates `gdbus-codegen` with
`find_program(... REQUIRED)`, so configure fails clearly if the GLib dev tooling
is absent.

Status: **implemented.**

One nuance: the output is generated **into the source tree** (`generated/`),
not into the build directory. CMake idiom favors `${CMAKE_CURRENT_BINARY_DIR}`
for generated sources, but writing them under `generated/` keeps them visible
while developing (readable, greppable, navigable by tooling) without committing
them. `.gitignore` keeps them out of version control, so the source-of-truth
guarantee holds.

## Considered options

- **Commit the generated code**: zero build-time dependency on
    `gdbus-codegen`; artifact readable in the repo. But it can silently diverge
    from the Introspect XML and inflates the tree with generated files.
- **Generate at build time** (chosen direction): `.xml` is the sole source of
    truth; no stale or committed artifacts. Costs a build-time dependency on
    `gdbus-codegen` (part of the GLib dev tooling) and a custom CMake command
    with correct output/dependency wiring.

## Consequences

- The `introspect/` and `generated/` split made this a build-only change: no
    file moves, only `CMakeLists.txt` wiring.
- `generated/` is build output: it is in `.gitignore` and no longer committed,
    yet remains readable in-tree during development.
- The build gains a hard dependency on `gdbus-codegen`; the shared
    `cmake/GdbusCodegen.cmake` discovers it with `find_program(... REQUIRED)` and
    configure fails clearly if it is absent.
- A clean checkout has an empty `generated/`; the first build populates it.
    Editing an `introspect/*.xml` regenerates its bindings on the next build.
