# 005 - Subscribing to D-Bus Signals

## The lesson

Connecting a handler to a signal is not always enough to receive it. Some D-Bus
services use an opt-in model: they only *emit* their broadcast signals while at
least one client has explicitly asked them to, by calling a service-specific
method such as systemd's `Subscribe()`.

There are two independent gates between a service's signal and your callback:

1. **Bus match rule** (D-Bus daemon level): your connection must have a match
    rule for the signal. `GDBusProxy` adds this automatically when you
    `g_signal_connect` to a generated proxy signal, so you rarely do it by hand.
2. **Service emit gate** (application level): *on top of* the match rule, some
    services refuse to emit certain signals unless a client has subscribed. This
    gate is service-specific and must be discovered per service.

Mental model:

- `g_signal_connect(...)` means "tell me when the signal arrives" (client side).
- `Subscribe()` means "service, please actually send the signals" (server side).

For systemd you need both, and `Subscribe()` must run *before* you issue work,
or a fast signal can arrive before you are listening.

## How to tell whether a service needs it

1. **Read the introspection XML** for a no-argument `Subscribe`/`Unsubscribe`
    (or similar) method near the signals. Its presence strongly implies an emit
    gate; its absence implies the service just broadcasts unconditionally.
2. **Read the service docs.** The XML shows the method *exists*, but only the
    docs state that signals are gated on it. This cannot be inferred from
    introspection alone.
3. **Test empirically.** Write a client that only connects the signal and does
    not subscribe; see if the handler fires. Caveat: `dbus-monitor` /
    `gdbus monitor` eavesdrop on all bus traffic and may show signals a normal
    client would not receive, so the monitor is not a reliable proof.

Rule of thumb:

- No `Subscribe`-like method in the XML -> just `g_signal_connect`.
- A `Subscribe`/`Unsubscribe` pair -> call it, confirm in the docs, and call it
    before you need the signals.

## systemd specifics

- `Subscribe()` and `Unsubscribe()` take no arguments and return nothing: they
    flip a single per-client "send me everything" switch, not a per-signal or
    per-unit subscription.
- Subscriptions are reference-counted per connection, and dropped automatically
    when the client disconnects. The PoC never calls `Unsubscribe()` because
    process exit cleans it up.
- In practice you may still receive `JobRemoved` without subscribing, if another
    client (a `systemctl`, a session manager) is already subscribed and systemd
    broadcasts bus-wide. Do not rely on this: call `Subscribe()` yourself so the
    signals flow regardless of who else is connected.
