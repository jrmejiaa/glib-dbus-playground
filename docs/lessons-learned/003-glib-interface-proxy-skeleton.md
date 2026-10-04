# 003 - GLib Types: Interface vs Proxy vs Skeleton

How GLib's `gdbus-codegen` represents one D-Bus interface (the protocol-level
contract from lesson 001) as C types. For each D-Bus interface the generator
emits **three** related types, and conflating them is the most common source of
confusion.

## The three types

For a D-Bus interface `Foo`, with namespace `Gdbus`, the generator produces:

- **`GdbusFoo`** — the **GInterface** (an abstract GObject *interface* type). It
    is the contract: the set of methods, the signal registrations, the property
    accessors. It is *not* something you instantiate.
- **`GdbusFooProxy`** — the **Proxy**, a concrete GObject used on the **client**
    side. It implements `GdbusFoo` and inherits `GDBusProxy`. When you call a
    method on it, it marshals a D-Bus message to the remote service.
- **`GdbusFooSkeleton`** — the **Skeleton**, a concrete GObject used on the
    **server** side to *export* and implement the interface. A service that
    *provides* `Foo` subclasses the skeleton and handles incoming calls.

Rule of thumb: **a client uses the Proxy; a server uses the Skeleton; both
"are" the Interface.** In this project the client talks to systemd, so it uses
only the Proxy. systemd owns the skeleton; we never touch it.

## Why the GInterface has no implementation in C

A real surprise when reading the generated header: the interface type is
*declared* but never *defined*. For this project:

```c
struct _GDbusOrgFreedesktopSystemd1Manager;                       // forward decl, no body
typedef struct _GDbusOrgFreedesktopSystemd1Manager
        GDbusOrgFreedesktopSystemd1Manager;

struct _GDbusOrgFreedesktopSystemd1ManagerIface                   // the actual contents
{
    GTypeInterface parent_iface;
    /* ... method/signal vtable slots ... */
};
```

This is intentional, not missing code. In GObject a **GInterface is abstract**:
it is a *type tag plus a vtable* (`...Iface`), never a struct with data members
you allocate. The forward declaration `struct _..Manager;` exists only to give
the pointer a distinct type; the real substance lives in the `...Iface` vtable.
So there is nothing to implement for the interface struct itself — looking for
its definition is looking for something that, by design, does not exist.

The concrete object you actually get is the **Proxy**, whose struct *does* have a
body:

```c
struct _GDbusOrgFreedesktopSystemd1ManagerProxy
{
    GDBusProxy parent_instance;                                   // it IS a GDBusProxy
    GDbusOrgFreedesktopSystemd1ManagerProxyPrivate *priv;
};
```

## How the pieces connect at runtime

1. `..._proxy_new_finish()` returns a pointer **typed as the interface**
    (`GDbusOrgFreedesktopSystemd1Manager *`), but the object behind it is really
    a `...ManagerProxy` (which is a `GDBusProxy`, which is a `GObject`).
2. The "methods" are **free functions**, not struct members:
    `gdbus_..._manager_call_start_unit(manager, ...)`. Internally each does
    `g_dbus_proxy_call(G_DBUS_PROXY(proxy), ...)`, casting the interface pointer
    down to the proxy. You never dereference the interface pointer yourself.
3. Signals (e.g. `job-removed`) are registered on the proxy GObject; you connect
    with `g_signal_connect` (see lesson 002 for the name mapping).

## Takeaway

- D-Bus "interface" (lesson 001) = a named contract on the wire.
- GLib renders that contract as an **abstract GInterface** (`GdbusFoo`) plus two
    concrete GObjects: a **Proxy** (client) and a **Skeleton** (server).
- The GInterface having no struct body is correct GObject design — the vtable
    (`...Iface`) is where the contract lives.
- A D-Bus client only ever needs the Proxy.
