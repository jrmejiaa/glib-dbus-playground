#!/usr/bin/env python3
"""Experiment B for lesson 006: react to ActiveState, enrich from the cache.

Tests the "react to ActiveState, then read SubState/Result/ExecMainStatus"
design, and whether reading those companions FROM THE PROXY CACHE is race-free.

Design under test:
  * Create one cached proxy per interface (Gio.DBusProxy caches properties and
    tracks PropertiesChanged for us).
  * React only when ActiveState actually appears in a change batch -- i.e. treat
    it like `notify::active-state`. Everything else is ignored.
  * When ActiveState == failed, read SubState (Unit cache) and
    Result/ExecMainStatus/NRestarts (Service cache) -- NO fresh Get, just the
    cache the proxy already maintains.

Two things it measures:
  1. EFFICIENCY: how many raw change batches arrive vs how many carried
     ActiveState (= how many times the real logic would run).
  2. CORRECTNESS: when we react to failed and read the Service cache, do we see
     the failure cause (Result=signal, ExecMainStatus=9) or has the auto-restart
     already overwritten it to success/0?  Run with 'block' to stress it.

Run it through the self-contained runner (recommended):

    ./run.sh cache

or standalone against a pre-existing unit named poc-race.service:

    python3 cache-probe.py block

Note: with Restart=on-failure the unit stays loaded (active/auto-restart), so the
object path survives for the duration of the test. On a clean stop it would be
garbage-collected (see ADR 0005).
"""
import subprocess
import sys
import time

from gi.repository import Gio, GLib

UNIT = "poc-race.service"
BLOCK = len(sys.argv) > 1 and sys.argv[1] == "block"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

# ADR 0004: without Subscribe(), systemd emits no PropertiesChanged at all.
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe", None, None,
              Gio.DBusCallFlags.NONE, -1, None)

# Resolve the unit object path (unit must be loaded).
res = bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                    "org.freedesktop.systemd1.Manager", "GetUnit",
                    GLib.Variant("(s)", (UNIT, )), None,
                    Gio.DBusCallFlags.NONE, -1, None)
unit_path = res.unpack()[0]

# One cached proxy per interface on the same object. Gio.DBusProxy fetches and
# caches all properties, and keeps the cache updated from PropertiesChanged.
unit_proxy = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                                    "org.freedesktop.systemd1", unit_path,
                                    "org.freedesktop.systemd1.Unit", None)
svc_proxy = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                                   "org.freedesktop.systemd1", unit_path,
                                   "org.freedesktop.systemd1.Service", None)

events = []
t0 = time.monotonic()
stats = {"unit_batches": 0, "activestate_batches": 0}


def stamp():
    return f"{(time.monotonic() - t0) * 1000:7.1f}ms"


def cached(proxy, name):
    v = proxy.get_cached_property(name)
    return v.unpack() if v is not None else None

def on_unit_removed(conn, sender, path, iface, signal, params):
    _id, unit = params.unpack()
    if unit == UNIT:
        events.append(f"[{stamp()}] UnitRemoved")


def on_job_new(conn, sender, path, iface, signal, params):
    _id, _, unit = params.unpack()
    if unit == UNIT:
        events.append(f"[{stamp()}] JobNew")


def on_job_removed(conn, sender, path, iface, signal, params):
    _id, _, unit, result = params.unpack()
    if unit == UNIT:
        events.append(f"[{stamp()}] JobRemoved result={result}")

def on_unit_changed(proxy, changed, invalidated):
    stats["unit_batches"] += 1
    d = changed.unpack()
    if "ActiveState" not in d:
        return  # <-- this is the "only react to ActiveState" filter
    stats["activestate_batches"] += 1
    state = d["ActiveState"]
    line = f"[{stamp()}] ActiveState -> {state:12} SubState={cached(unit_proxy,'SubState'):12}"
    line += (f"  | FROM CACHE: Result={cached(svc_proxy,'Result')} "
             f"ExecMainStatus={cached(svc_proxy,'ExecMainStatus')} "
             f"NRestarts={cached(svc_proxy,'NRestarts')}")
    events.append(line)


bus.signal_subscribe("org.freedesktop.systemd1",
                     "org.freedesktop.systemd1.Manager", "UnitRemoved",
                     "/org/freedesktop/systemd1", None,
                     Gio.DBusSignalFlags.NONE, on_unit_removed)
bus.signal_subscribe("org.freedesktop.systemd1",
                     "org.freedesktop.systemd1.Manager", "JobNew",
                     "/org/freedesktop/systemd1", None,
                     Gio.DBusSignalFlags.NONE, on_job_new)
bus.signal_subscribe("org.freedesktop.systemd1",
                     "org.freedesktop.systemd1.Manager", "JobRemoved",
                     "/org/freedesktop/systemd1", None,
                     Gio.DBusSignalFlags.NONE, on_job_removed)

unit_proxy.connect("g-properties-changed", on_unit_changed)

loop = GLib.MainLoop()


def kick():
    pid = cached(svc_proxy, "MainPID")
    events.append(f"[{stamp()}] SIGKILL {pid}" +
                  (" + BLOCK 1s" if BLOCK else ""))
    subprocess.run(["kill", "-9", str(pid)], check=False)
    if BLOCK:
        time.sleep(1.0)
        events.append(f"[{stamp()}] unblocked, draining")
    return False


def stop_test():
    loop.quit()
    return False


GLib.timeout_add(300, kick)
GLib.timeout_add(4000, stop_test)
loop.run()

print("\n".join(events))
print(
    f"\n[stats] Unit change batches total={stats['unit_batches']}, "
    f"carried ActiveState={stats['activestate_batches']} "
    f"(logic ran {stats['activestate_batches']}x instead of {stats['unit_batches']}x)"
)
