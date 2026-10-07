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
