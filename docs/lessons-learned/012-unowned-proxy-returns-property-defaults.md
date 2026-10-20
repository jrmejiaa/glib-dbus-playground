# 012 - An Unowned Proxy Silently Returns Property Defaults

## The lesson

A `GDBusProxy` created for a well-known name that **no one owns** still constructs
successfully. The async `..._proxy_new` finishes without error, you get a live
proxy object — but its property cache is **empty**, because the construction-time
`GetAll` had no one to answer it. The generated typed getter then returns the C
**default for the type** (`0` for a `guint`) rather than signalling "no value":

```c
static guint
poc_daemon_proxy_get_state (PocDaemon *object)
{
  ...
  guint value = 0;                                  /* default when absent */
  variant = g_dbus_proxy_get_cached_property (proxy, "State");
  if (variant != NULL)                              /* only overwritten if cached */
    value = g_variant_get_uint32 (variant);
  ...
  return value;
}
```

If the property is a `uint` enum that starts at `0`, that default **collides with
a real, valid state**. So an absent daemon, or a name you mistyped, does not fail
loudly — it masquerades as a daemon sitting at state `0`. And because nothing owns
the name, no `PropertiesChanged` will ever arrive, so `notify::<prop>` never fires
(lesson 011). The result looks exactly like "the daemon is stuck in its initial
state," which sends you debugging the daemon instead of the connection.

## How this bit us: a stale binary after a bus-name rename

We renamed the daemon interface and well-known name `es.poc.Daemon` ->
`org.poc.Daemon` and rebuilt the Supervisor. The Supervisor's `DaemonStateWatcher`
now proxied `org.poc.Daemon<N>`. But the **installed daemon binary was not rebuilt**,
so the running units still owned the old `es.poc.Daemon<N>`:

```text
$ busctl --user list | grep -i poc
es.poc.Daemon1   ...   poc-daemon   ...        <- still the old name
$ journalctl --user -u poc-dummy@3.service
... owning name es.poc.Daemon3                 <- stale binary
```

So `org.poc.Daemon3` had no owner. The watcher's proxy built fine, `get_state()`
returned the `0` default — which decoded to `initializing` — and `notify::state`
stayed silent forever. The symptom ("proxy ready, current state 'initializing',
but no change callbacks") pointed at the daemon; the cause was that **nobody was
home at the name the proxy was watching**.

## The two-part fix

### In code: distinguish "no owner / empty cache" from a real zero

- Check the owner. `g_dbus_proxy_get_name_owner()` is `NULL` exactly when the
    well-known name is unowned. Treat that as a loud, named warning, not a state:

    ```cpp
    gchar *pOwner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (m_pDbusDaemon));
    if (pOwner == nullptr)
    {
        /* unowned name: do not report the 0-default as a real state */
        log ("WARNING: well-known name '%s' has no owner", m_WellKnownName.c_str ());
        return;
    }
    g_free (pOwner);   /* GLib owns the returned string */
    ```

- Probe the cache directly. `g_dbus_proxy_get_cached_property(proxy, "State")`
    returns `NULL` when there is no cached value, which the typed getter hides
    behind its `0` default. Map that `NULL` to your own `unknown` sentinel so an
    unpopulated proxy never reads as a valid state.

- Keep listening anyway. `GDBusProxy` tracks the well-known name: if an owner
    appears later it reloads properties and emits `notify::<prop>`, so connect the
    handler even when the name is currently unowned.

### Operationally: a bus-name rename is not done until the owner is rebuilt

Renaming a name in source and rebuilding the **consumer** is not enough. The
process that **owns** the name must be rebuilt, reinstalled, and restarted —
otherwise a running stale binary keeps owning the old name while the new name has
no owner. After any `*.Name` rename, verify on the wire:

```sh
busctl --user list | grep -i <name>
```

## Checklist

1. Creating a proxy for a well-known name? Remember the proxy succeeds even with
    no owner, and typed getters return type defaults for an empty cache.
2. If the property's zero value is a meaningful state, never trust the getter
    alone — check `g_dbus_proxy_get_name_owner()` and/or a `NULL`
    `g_dbus_proxy_get_cached_property()`.
3. Map "no cached value" to an explicit `unknown`, and log the unowned case.
4. After renaming a bus name, rebuild/reinstall/restart the **owner**, and
    confirm with `busctl --user list`.
