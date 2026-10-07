# 010 - Deriving GObject signal callback signatures (and the notify:: GParamSpec)

## The lesson

A GObject signal callback has a fixed shape, and getting it wrong does not fail to
compile — `G_CALLBACK()` casts the function pointer, so a mismatched signature
compiles cleanly and then **segfaults at runtime** when you dereference a
misaligned `user_data`.

The rule for deriving any signal's callback signature:

```text
callback = (emitting_instance, <signal-specific args...>, gpointer user_data)
            ^ always first                                 ^ always last (g_signal_connect)
```

- The **emitting instance** (the object you connected to) is always the first
    argument — not your own object.
- The **signal-specific arguments** are defined by that particular signal.
- Your **`user_data`** is always the last argument. `g_signal_connect` delivers it
    for every signal, including property notifications.

If you drop or add a middle argument, every later argument shifts by one slot, so
`user_data` is read from the wrong place → a garbage pointer → a crash.

## The surprise: notify:: carries a GParamSpec

`notify` is **one signal defined once on `GObject`**, with a fixed signature:

```c
void notify(GObject *object, GParamSpec *pspec);   /* + trailing user_data */
```

So every `notify` handler is:

```c
void on_notify(GObject *object, GParamSpec *pspec, gpointer user_data);
```

Two things that trip people up:

- `notify::active-state` is **not** a separate per-property signal. The
    `::active-state` is a signal **detail** — a filter on the single `notify` signal
    — so the signature is identical no matter which property, or none.
- The middle `GParamSpec *pspec` exists **because** one generic signal serves all
    properties: `object` says *which instance* changed, `pspec` says *which
    property*. It does **not** carry the new value — `notify` only says "this
    changed"; you read the value yourself from the object or the proxy cache.

Omitting the `pspec` is the classic bug:

```c
/* WRONG: user_data lands in the pspec slot -> self is garbage -> segfault */
void on_changed(GObject *object, gpointer user_data);

/* RIGHT */
void on_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
```

## Each signal has its own middle arguments

The trailing `user_data` is universal; the middle differs per signal. Three used in
this project:

| Signal (connect name)      | Defined on         | Callback signature                                                          |
|----------------------------|--------------------|-----------------------------------------------------------------------------|
| `notify::<prop>`           | `GObject`          | `(GObject *obj, GParamSpec *pspec, gpointer user_data)`                      |
| `g-properties-changed`     | `GDBusProxy`       | `(GDBusProxy *proxy, GVariant *changed, GStrv invalidated, gpointer user_data)` |
| `unit-new` (systemd Mgr)   | generated proxy    | `(GDbusSystemd1Manager *mgr, const gchar *id, const gchar *path, gpointer user_data)` |

Note how easy it is to conflate them: a Supervisor's `unit-new`/`unit-removed`
handlers and a `UnitMonitor`'s `notify::active-state` handler all put `user_data`
last, but their *middle* arguments differ. Copying one handler's shape onto another
signal is exactly how the slot shift happens.

## How to find the right signature

- For a **generated proxy** signal, read the generated header: `gdbus-codegen`
    emits a matching `*_callback` typedef and documents each signal's parameters
    (see lesson 004 for reading the generated code).
- For **`notify`**, it is always `(object, GParamSpec*, user_data)` — memorize it.
- For a stock GObject/Gio signal, its documentation lists the parameters; prepend
    the instance and append `user_data`.

## Why it compiles anyway

`g_signal_connect` takes a `GCallback` (a generic `void (*)(void)`), and
`G_CALLBACK()` casts your function to it, discarding the real parameter list. The
compiler therefore cannot check the signature; the mismatch only shows up when GLib
invokes the callback with the signal's true argument layout. Treat the signature as
something you must get right by construction, because the compiler will not catch it.
