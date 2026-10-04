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
