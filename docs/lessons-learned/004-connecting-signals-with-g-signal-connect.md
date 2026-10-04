# 004 - Connecting Signals with g_signal_connect

How to connect a handler to a generated GObject signal and get the callback
signature right. Both facts you need — the signal *name* and the callback
*parameters* — are determined by the generated C code, not by the XML directly.
Lesson 002 covers the name *spelling* rules; this lesson is about reading the
generated code to connect correctly, and about the method-vs-signal asymmetry
that trips you up.

## Always read the generated .c for the real signal name

`g_signal_connect(instance, "name", G_CALLBACK(cb), user_data)` matches the
signal name as a **plain string** at runtime. A wrong name is not a compile
error — just a GLib warning and a handler that never fires. So the name must be
exactly what the generator registered.

The authoritative source is the `g_signal_new(...)` calls in the generated
`.c`, not your memory of the XML. Search for them:

```sh
grep -nE 'g_signal_new' gdbus-alarm.c
```

For the alarm interface this shows the two actual names:

```c
g_signal_new ("handle-configure", ...   /* from XML method  <method name="Configure"> */
g_signal_new ("beeeeeeeeep",      ...   /* from XML signal  <signal name="Beeeeeeeeep"> */
```

Note `Configure` (a method) did **not** become `"configure"` — it became
`"handle-configure"`. That prefix is the key asymmetry below.

## Methods and signals produce different GObject signals

A D-Bus interface member becomes a GObject signal in one of two shapes depending
on whether it is a *method* or a *signal* in the XML:

| XML member                      | GObject signal name | Returns   | Who connects it |
|---------------------------------|---------------------|-----------|-----------------|
| `<method name="Configure">`     | `handle-configure`  | `gboolean`| the **service** |
| `<signal name="Beeeeeeeeep">`   | `beeeeeeeeep`        | `void`    | a **client**    |

- A **method** becomes a service-side `handle-<method>` signal. The service
    connects it to *answer* incoming calls, and returns `TRUE` to indicate the
    call was handled. This is the Skeleton side (lesson 003).
- A **signal** becomes a `<signal>` GObject signal that a client connects to
    *receive* a broadcast. This is the Proxy side.

So `handle-configure` and `beeeeeeeeep` are not two of the same thing: one is
how a server fields a request, the other is how a client receives an event.

## Deriving the callback signature from the Iface vtable

The parameters your callback must take are defined by the interface vtable
struct at the top of the generated header (`_AlarmIface` for alarm). Each slot's
function-pointer signature *is* the callback signature — minus the instance
nuance, plus one addition. For alarm:

```c
struct _AlarmIface
{
  GTypeInterface parent_iface;

  gboolean (*handle_configure) (        /* <- signal "handle-configure" */
    Alarm *object,
    GDBusMethodInvocation *invocation,
    guint arg_seconds);

  gboolean (*get_activated) (Alarm *object);

  void (*beeeeeeeeep) (                  /* <- signal "beeeeeeeeep" */
    Alarm *object);
};
```

So the handler for `"handle-configure"` takes
`(Alarm *object, GDBusMethodInvocation *invocation, guint arg_seconds)` and
returns `gboolean`; the handler for `"beeeeeeeeep"` takes `(Alarm *object)` and
returns `void`. The vtable slot name (`handle_configure`, snake_case) and the
signal string (`handle-configure`, hyphenated) are the same name in the two
spellings from lesson 002.

### The one addition: trailing `gpointer user_data`

The vtable slot does **not** show the final argument every `g_signal_connect`
handler receives: the `user_data` pointer you passed as the last argument to
`g_signal_connect`. GLib appends it after the signal's own parameters. So the
rule is:

> callback signature = the Iface vtable slot, **plus a trailing `gpointer user_data`**.

The repo's systemd client is the worked example. The `job_removed` vtable slot
is:

```c
void (*job_removed) (
    GDbusOrgFreedesktopSystemd1Manager *object,
    guint arg_id,
    const gchar *arg_job,
    const gchar *arg_unit,
    const gchar *arg_result);
```

and the connected handler adds the trailing `user_data`:

```cpp
// 5 signal params from the vtable slot + 1 trailing gpointer:
static void onJobRemoved(DbusSystemMgr *manager,
                         guint id, const gchar *job,
                         const gchar *unit, const gchar *result,
                         gpointer userData);

g_signal_connect(m_Manager, "job-removed",
                 G_CALLBACK(&onJobRemoved), self);  // `self` arrives as userData
```

`G_CALLBACK` casts the typed function pointer to the generic `GCallback` type so
the compiler accepts it; it does **not** check the parameters. Getting them
wrong is undefined behavior at call time, not a compile error — which is exactly
why you derive them from the vtable rather than guessing.

`GCallback` is `typedef void (*GCallback)(void)` — a signature-less pointer. It
is only the *storage* type: at emission GLib ignores it and invokes your handler
through the signal's **registered** prototype (the marshaller plus the
`g_signal_new` return type), reading the return value from the slot that
prototype dictates. So the real return type must match what was registered, even
though `G_CALLBACK` hides the mismatch from the compiler.

## Getting the return type wrong: the `G_IS_OBJECT` CRITICAL

The most common way to hit this: a service connects a `handle-*` signal with a
handler declared `void` instead of `gboolean`. It compiles (thanks to
`G_CALLBACK`) and even seems to work, but every call logs:

```
GLib-GObject-CRITICAL **: g_object_unref: assertion 'G_IS_OBJECT (object)' failed
```

Why: `handle-configure` is registered `G_TYPE_BOOLEAN` with the
`g_signal_accumulator_true_handled` accumulator. The generated skeleton
dispatcher emits the signal, then checks the boolean result:

```c
g_signal_emitv (paramv, signal_id, 0, &return_value);
if (!g_value_get_boolean (&return_value))   /* no handler claimed it? */
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
        G_DBUS_ERROR_UNKNOWN_METHOD, ...);  /* so dispatcher completes it */
```

A `void` handler never writes the return slot, so the accumulator sees `FALSE`
and the dispatcher believes the method was **unhandled** — and completes the
invocation itself with an `UNKNOWN_METHOD` error. But your handler already
completed that same invocation (via `alarm_complete_configure()` or
`g_dbus_method_invocation_return_error*()`), which consumed and unref'd it. The
dispatcher's second completion then unrefs an already-freed invocation → the
`G_IS_OBJECT` assertion, on every call.

Fix: declare the handler `gboolean` and `return TRUE`
(`G_DBUS_METHOD_INVOCATION_HANDLED`) on **every** path that completes the
invocation — both the success branch and any error branch. Returning `TRUE`
means "I handled it; do not run the dispatcher's fallback."

Takeaway: for a `handle-*` (method) signal, returning `gboolean`/`TRUE` is not
optional sugar — it is how you tell the generated dispatcher to stop. A `void`
handler is the classic case of deriving the signature from `GCallback` (which
says `void`) instead of from the Iface vtable slot (which says `gboolean`).

## Checklist

1. Find the real signal string in the generated `.c`:
    `grep g_signal_new <file>.c`. Do not assume it equals the XML name
    (`Configure` -> `handle-configure`).
2. Decide which side you are on: a `handle-*` signal is service-side (answer
    calls, return `gboolean`); a plain signal is client-side (receive, return
    `void`).
3. Copy the matching vtable slot signature from the `..._Iface` struct in the
    generated header.
4. Append a trailing `gpointer user_data` to that signature.
5. Wrap the function in `G_CALLBACK` at the `g_signal_connect` call, and pass
    your context as the last argument — it arrives as `user_data`.
