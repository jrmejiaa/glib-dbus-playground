# 0003 - Wrap each GLib program in a class; callbacks are static member methods

Each D-Bus program is a single class that owns its GLib object and its
callbacks. In the alarm step that is `AlarmDbusServer`
(`service/alarm-service.hpp`/`.cpp`), which owns the skeleton, and
`AlarmDbusClient` (`client/alarm-client.hpp`/`.cpp`), which owns the proxy; the
systemd step follows the same shape with its client class. Every callback is a
**static** member function with the exact C signature GLib expects; it recovers
the instance from the user-data pointer
(`auto *self = static_cast<AlarmDbusClient *>(userData);`) and does the real work
directly, with no separate forwarding handler.

## Consequences

- Any string crossing the C boundary is converted to `std::string` once at the
  top of the callback, and GLib-allocated values are freed with `g_free`
  immediately; internal logic then uses only modern types. The alarm callbacks
  happen to pass only scalars (`guint seconds`, no string args), so the
  conversion is a no-op there — but the systemd client hits it for real with the
  job path, unit, and result strings carried by `JobRemoved` (ADR 0004).
- The class owns its GLib object (skeleton or proxy) and unrefs it in its
  destructor (RAII); copy and assignment are deleted.
- Console output uses `std::fprintf`, not `<iostream>`.
- No namespace is used: the class provides sufficient scoping, and file-local
  `constexpr` constants already have internal linkage.
