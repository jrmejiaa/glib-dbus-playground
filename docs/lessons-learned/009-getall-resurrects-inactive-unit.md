# 009 - A FLAGS_NONE proxy's GetAll resurrects an inactive unit

## The lesson

Constructing a `GDBusProxy` with `G_DBUS_PROXY_FLAGS_NONE` on a systemd unit is
**not an inert, client-side act**. At construction the proxy issues a
`GetAll` on the unit's properties, and answering that forces systemd to **load**
the unit if it is not already loaded — emitting `UnitNew`. Because a just-stopped
unit is inactive and otherwise unreferenced, systemd then **garbage-collects it
again** — emitting `UnitRemoved`. So merely *looking at* a dead unit resurrects it
for one load/GC cycle.

This turns a naive observer into an infinite loop. A Supervisor that builds a
proxy on `UnitNew` and drops it on `UnitRemoved` will:

```text
UnitRemoved -> drop proxy
            -> (burst) UnitNew -> build proxy -> GetAll RESURRECTS the dead unit
            -> UnitNew/UnitRemoved -> drop -> build -> resurrect -> ... forever
```

A passive observer that never calls `GetAll` sees the same clean-stop burst and it
simply dies out. The difference is entirely the `GetAll` side effect.

Both facts are reproducible with the scripts in [`scripts/`](./scripts); each
creates its own throwaway `--user` unit and cleans up.

## Experiment 1 - the loop vs a passive observer

[`scripts/loop-repro-probe.py`](./scripts/loop-repro-probe.py) mimics the
Supervisor (build a proxy on `UnitNew`, drop it on `UnitRemoved`) and caps events
so a real loop cannot run forever.

```sh
python3 scripts/loop-repro-probe.py mimic     # build+hold+drop -> loops
python3 scripts/loop-repro-probe.py passive   # observe only    -> settles
```

Observed, after a single `systemctl stop`:

```text
mimic   : UnitRemoved -> UnitNew -> UnitRemoved -> UnitNew -> ...  (hits the cap; loop)
passive : UnitRemoved -> UnitNew -> UnitRemoved  (5 events, then silence; settles)
```

## Experiment 2 - isolate the cause: GetAll resurrects a gone unit

[`scripts/access-reload-probe.py`](./scripts/access-reload-probe.py) lets a unit
settle fully gone, confirms it is unloaded, then builds one proxy.

```sh
python3 scripts/access-reload-probe.py
```

Observed:

```text
GetUnit failed -> unloaded (Unit poc-access.service not loaded.)
>>> building Unit proxy (GetAll) on the gone unit's path
proxy built (GetAll returned)
UnitNew        # <-- the GetAll loaded it
UnitRemoved    # <-- systemd GC'd it right back
```

A single `GetAll` on a confirmed-gone unit produced a `UnitNew`/`UnitRemoved` pair
out of nothing. That is the engine of the loop.

## Experiment 3 - DO_NOT_LOAD_PROPERTIES removes the side effect

[`scripts/do-not-load-probe.py`](./scripts/do-not-load-probe.py) does the same but
builds with `DO_NOT_LOAD_PROPERTIES` (no construction `GetAll`).

```sh
python3 scripts/do-not-load-probe.py
```

Observed:

```text
>>> build proxy with DO_NOT_LOAD_PROPERTIES (no GetAll)
proxy built (no GetAll)
VERDICT: NO resurrection
```

No `GetAll`, no resurrection — confirming the construction-time `GetAll` is the
whole cause.

## Design consequence

- **Never build a `FLAGS_NONE` proxy on a unit you have not confirmed is live.**
  The clean-stop revival debounce (ADR 0007) is what guarantees this: the per-unit
  monitor is kept until a short timer fires, so a `UnitNew` during the removal burst
  finds the monitor already present and does **not** rebuild — no `GetAll`, no
  resurrection. The debounce is therefore **correctness-critical**, not just an
  efficiency measure.
- **Correctness is independent of the window value.** The loop is broken by *not
  rebuilding while a removal is pending* (the monitor persists through the burst),
  which holds for any positive window; the window only tunes revival latency.
- **`DO_NOT_LOAD_PROPERTIES` is a root-cause defense.** Skipping the construction
  `GetAll` removes the resurrection side effect entirely, at the cost of the initial
  property snapshot (the cache then populates from the first `PropertiesChanged`).
  Useful as belt-and-suspenders; the debounce is still what ensures you only attach
  to genuinely-present units.

## Environment

Verified on systemd 252 (252.39-1~deb12u2), user/session instance, `python3` +
PyGObject. `Gio.DBusProxy.new_sync(..., FLAGS_NONE, ...)` stands in for the
generated `..._proxy_new_sync`; the scripts use only the per-user systemd instance
and throwaway transient units.
