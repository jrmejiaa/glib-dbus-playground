# 008 - The cost of synchronous proxy construction (GetAll blocks the loop)

## The lesson

A `GDBusProxy` built with `G_DBUS_PROXY_FLAGS_NONE` (the default, and what the
generated `..._proxy_new_sync` uses) performs a blocking `GetAll` round-trip at
construction to populate its property cache. On a quiet session bus that is a few
hundred microseconds — but the systemd **`Service` interface is large** (303
properties), so constructing a Unit + Service pair (one `UnitMonitor`) blocks the
calling thread for **~2.5 ms**. While a signal callback does that sync build, the
main loop pumps nothing: queued events are *delayed* (not dropped — see lesson 006),
so at scale a burst of constructions can stall observation.

The number is small enough to accept at PoC scale and large enough that you must
not pretend it is free. Measure, then decide. The reproducer is
[`scripts/sync-cost-probe.py`](./scripts/sync-cost-probe.py).

## Experiment - measure the blocking cost

```sh
python3 scripts/sync-cost-probe.py
```

Observed (20 iterations each, loaded unit, session bus):

```text
properties returned: Unit=98  Service=303
GetAll Unit                              avg=0.48ms  max=0.85ms
GetAll Service                           avg=1.03ms  max=1.45ms
proxy_new_sync Unit  (GetAll+cache)      avg=0.85ms  max=1.32ms
proxy_new_sync Service (GetAll+cache)    avg=1.46ms  max=1.58ms
UnitMonitor (Unit+Service) per UnitNew   avg=2.48ms  max=2.97ms
```

The `Service` interface's 303-property `GetAll` dominates. One `UnitMonitor` build
blocks ~2.5 ms, ~3 ms worst case.

## Design consequence

- **At PoC scale (3 units): sync construction is fine.** Launch builds three
    monitors (~7.5 ms total, spread across callbacks); events that arrive meanwhile
    queue and process a few ms late — imperceptible.
- **Build only on a genuine appearance.** Paired with the debounce in lesson 007 /
    ADR 0007, a `UnitMonitor` is built once per real load (initial start or a revived
    unit's `UnitNew`), never per `systemctl`-flap event — so the ~2.5 ms is never
    multiplied by the burst.
- **At fleet scale, move the build off the signal callback.** The cost grows
    linearly; defer the sync build to a `g_idle` source, or switch to async
    `..._proxy_new` + `_finish` (which then needs a pending-state and handling for a
    unit that vanishes before construction finishes).
- **Do not avoid the cost with `DO_NOT_LOAD_PROPERTIES`.** The populated cache is
    exactly what lets you read companion properties from the cache inside the change
    handler (lesson 006); skipping the initial `GetAll` trades this one-time cost for
    per-read round-trips.

## Environment

Verified on systemd 252 (252.39-1~deb12u2), user/session instance, `python3` +
PyGObject. `Gio.DBusProxy.new_sync(..., FLAGS_NONE, ...)` is the measured stand-in
for the generated `..._proxy_new_sync`.
