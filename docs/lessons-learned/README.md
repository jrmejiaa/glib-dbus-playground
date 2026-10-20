# Lessons Learned

Reusable notes on D-Bus and GLib, gathered while building this PoC. Numbered in
conceptual reading order: fundamentals first, then tooling, then GLib's type
model, then service-specific behavior.

## Index

1. [D-Bus Concepts](./001-dbus-concepts.md): bus, well-known name, object path,
    interface, method, signal, property — the protocol-level vocabulary.
2. [Tooling](./002-gdbus-and-codegen-tooling.md): `gdbus`, `gdbus-codegen`,
    `busctl`, and monitors — exploring a service and generating C bindings.
3. [GLib Types](./003-glib-interface-proxy-skeleton.md): Interface vs Proxy vs
    Skeleton, and why the GInterface is abstract with no C implementation.
4. [Connecting Signals](./004-connecting-signals-with-g-signal-connect.md):
    reading the generated code for the real signal name and deriving the
    callback signature from the Iface vtable (plus a trailing `user_data`).
5. [Subscribing to Signals](./005-subscribing-to-dbus-signals.md): why
    connecting a handler is not always enough, and systemd's `Subscribe()`.
6. [Event Stream Is Lossless](./006-event-stream-is-lossless-pull-races.md):
    queued D-Bus signals survive a blocked loop, while a fresh `Get` after the
    fact races the auto-restart — with two runnable reproducers in `scripts/`.
7. [UnitNew/UnitRemoved Semantics](./007-unitnew-unitremoved-semantics.md): the
    clean-stop burst is a `systemctl` CLI artifact, the object path is stable, and
    `UnitRemoved` means "cleanly gone" (never a crash) — three reproducers.
8. [Sync Proxy Construction Cost](./008-sync-proxy-construction-cost.md): a
    `FLAGS_NONE` proxy does a blocking `GetAll`; the systemd `Service` interface is
    303 properties, so one `UnitMonitor` build blocks ~2.5 ms — measured.
9. [GetAll Resurrects an Inactive Unit](./009-getall-resurrects-inactive-unit.md):
    a `FLAGS_NONE` proxy's construction `GetAll` loads a dead unit (`UnitNew`) which
    systemd GCs again (`UnitRemoved`), turning a naive observer into an infinite
    loop — why the clean-stop debounce is correctness-critical.
10. [Signal Callback Signatures](./010-notify-and-signal-callback-signatures.md):
    deriving any GObject signal callback (instance first, `user_data` last), and the
    surprise that `notify::` carries a `GParamSpec` middle argument — omit it and
    `user_data` shifts into the wrong slot and segfaults.
11. [EmitsChangedSignal Annotation](./011-emitschangedsignal-annotation.md): the
    annotation is a contract, not a switch — it never grants `notify::`, an absent
    annotation defaults to `true`, and the bit is load-bearing only on the
    gdbus-codegen server (skeleton), never on the client proxy.
12. [Unowned Proxy Returns Property Defaults](./012-unowned-proxy-returns-property-defaults.md):
    a proxy for an unowned well-known name constructs fine but has an empty cache, so
    typed getters return the type default (`0`) — which masquerades as a real
    zero-value state; after a bus-name rename, a stale owner binary reproduces this
    exactly. Guard with `g_dbus_proxy_get_name_owner()` / a `NULL` cached property.
