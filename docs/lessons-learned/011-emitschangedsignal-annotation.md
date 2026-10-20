# 011 - The EmitsChangedSignal Annotation Is a Contract, Not a Switch

## The lesson

`org.freedesktop.DBus.Property.EmitsChangedSignal` does **not** create a signal,
and it does **not** grant the ability to connect `notify::<prop>`. A D-Bus
property never becomes its own GObject signal the way a `<method>` or `<signal>`
does (lesson 004); change-notification always rides on two pre-existing, generic
channels:

- On the **wire**: the one `org.freedesktop.DBus.Properties.PropertiesChanged`
    signal, shared by every property on the interface.
- In **GObject**: the one `notify` signal, filtered by detail — `notify::state`
    (lesson 010).

Both of those exist for *every* property regardless of any annotation.
gdbus-codegen always generates the getter and the `notify` plumbing. So
`g_signal_connect(proxy, "notify::state", ...)` always compiles and always
connects. Whether your handler ever *fires* is a separate, runtime question:
does a `PropertiesChanged` actually arrive? The annotation is the **contract that
declares the answer** — it is not the mechanism.

## The trap: absent does not mean "no signal"

The confusing case is a property with no annotation at all, e.g. ModemManager's:

```xml
<property name="AccessTechnologies" type="u" access="read" />
```

You can connect `notify::access-technologies` and it fires — which looks like
proof the annotation is pointless. It is not. Per the D-Bus specification, an
**absent annotation defaults to `true`**. The property is therefore declared to
emit; the handler fires *because* of the default, not in spite of a missing
annotation.

The four values, and what a consumer may rely on:

| Value         | Wire behavior                                                      | Rely on `notify::`?                     |
|---------------|-------------------------------------------------------------------|-----------------------------------------|
| `true`        | `PropertiesChanged` emitted **with the new value**                | Yes — value arrives in the signal       |
| `invalidates` | `PropertiesChanged` emitted **without** the value (name invalid)  | Partly — get "changed", then re-`Get`   |
| `const`       | Never changes for the object's lifetime; signal **never** emitted | No — and never needed; cache forever    |
| `false`       | May change, but **no signal is emitted**                          | No — you must poll / re-`Get`            |
| *(absent)*    | Same as `true`                                                    | Yes                                     |

An interface-level annotation can change the default for all properties in that
interface; a per-property annotation overrides it.

Connecting `notify::` to a genuinely `const` or `false` property still compiles
and connects — and then never fires. That is exactly the contract the annotation
is warning you about in advance, so you poll instead of waiting forever.

## Where the bit is load-bearing: the server, not the client

In the generated code the annotation is honored on **one** side only.

- **Server (skeleton)** — honors it. `poc_daemon_skeleton_set_property` gates
    emission on `info->emits_changed_signal`:

    ```c
    if (... get_connection (...) != NULL && info->emits_changed_signal)
        _poc_daemon_schedule_emit_changed (skeleton, info, prop_id, ...);
    g_object_notify_by_pspec (object, pspec);   /* local notify fires regardless */
    ```

    If `State` were `false`/`const`, `poc_daemon_set_state()` would update the
    local value and fire a *local* `notify`, but emit **nothing on the bus** — a
    consumer would never hear it.

- **Client (proxy)** — ignores it. `poc_daemon_proxy_g_properties_changed`
    re-emits `notify::` for whatever arrives in the received `PropertiesChanged`
    and never consults the bit. This is why, as a pure client of ModemManager,
    the annotation's value is invisible to you: you simply react to whatever the
    service chooses to send.

## Consequence for `org.poc.Daemon`

`State` is annotated `value="true"`, which is **behaviorally identical to
omitting it** — both default to emit-with-value. The explicit `true` is there to
document intent. Its only teeth would appear if it were changed to `false`/
`const`: the gdbus-codegen *server* would go silent on the bus and the
"Supervisor reacts without polling" design would break, even though
`poc_daemon_set_state()` would still look like it works locally.

## Rule of thumb

- Reading someone else's property? Read the annotation (remembering absent =
    `true`) to decide whether to wait on `notify::` or to poll. Do not infer
    "no signal" from a missing annotation.
- Writing your own gdbus-codegen service? The annotation is the switch that
    decides whether your skeleton emits `PropertiesChanged` at all. Set it
    deliberately; the default is `true`.
- `notify::` connecting successfully proves nothing about whether the signal
    will ever come — that is the annotation's job to declare.
