#!/usr/bin/env python3
"""How long does a synchronous Unit/Service proxy construction actually block?

A proxy built with FLAGS_NONE does a blocking GetAll round-trip (the generated
..._proxy_new_sync path). The systemd Service interface is large, so this times
the real cost on a loaded unit: raw GetAll per interface, and a full cached-proxy
construction per interface. Numbers decide whether sync-in-callback is acceptable
for the Supervisor, especially during a systemctl-stop flap that can fire several
UnitNew (each a new path) back to back.
"""
import subprocess
import time

from gi.repository import Gio, GLib

UNIT = "poc-cost.service"
ITERS = 20

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe",
              None, None, Gio.DBusCallFlags.NONE, -1, None)

subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemd-run", "--user", f"--unit={UNIT}", "/bin/sh", "-c", "sleep 300"],
               stderr=subprocess.DEVNULL, check=False)
time.sleep(1)

res = bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                    "org.freedesktop.systemd1.Manager", "GetUnit",
                    GLib.Variant("(s)", (UNIT,)), None, Gio.DBusCallFlags.NONE, -1, None)
path = res.unpack()[0]


def stats(label, times_ms):
    times_ms.sort()
    n = len(times_ms)
    avg = sum(times_ms) / n
    print(f"{label:38} min={times_ms[0]:.3f}ms  avg={avg:.3f}ms  "
          f"max={times_ms[-1]:.3f}ms  p50={times_ms[n // 2]:.3f}ms")


def time_getall(iface):
    out = []
    for _ in range(ITERS):
        t = time.perf_counter()
        bus.call_sync("org.freedesktop.systemd1", path,
                      "org.freedesktop.DBus.Properties", "GetAll",
                      GLib.Variant("(s)", (iface,)), None, Gio.DBusCallFlags.NONE, -1, None)
        out.append((time.perf_counter() - t) * 1000)
    return out


def time_proxy(iface):
    out = []
    for _ in range(ITERS):
        t = time.perf_counter()
        Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                               "org.freedesktop.systemd1", path, iface, None)
        out.append((time.perf_counter() - t) * 1000)
    return out


UNIT_IFACE = "org.freedesktop.systemd1.Unit"
SVC_IFACE  = "org.freedesktop.systemd1.Service"

# How many properties does each GetAll return? (size of the blocking payload)
unit_props = len(bus.call_sync("org.freedesktop.systemd1", path, "org.freedesktop.DBus.Properties",
                               "GetAll", GLib.Variant("(s)", (UNIT_IFACE,)), None,
                               Gio.DBusCallFlags.NONE, -1, None).unpack()[0])
svc_props = len(bus.call_sync("org.freedesktop.systemd1", path, "org.freedesktop.DBus.Properties",
                              "GetAll", GLib.Variant("(s)", (SVC_IFACE,)), None,
                              Gio.DBusCallFlags.NONE, -1, None).unpack()[0])

print(f"properties returned: Unit={unit_props}  Service={svc_props}\n")
stats("GetAll Unit", time_getall(UNIT_IFACE))
stats("GetAll Service", time_getall(SVC_IFACE))
stats("proxy_new_sync Unit (GetAll+cache)", time_proxy(UNIT_IFACE))
stats("proxy_new_sync Service (GetAll+cache)", time_proxy(SVC_IFACE))

# Worst case for one UnitNew: build BOTH proxies (what a UnitMonitor does).
both = []
for _ in range(ITERS):
    t = time.perf_counter()
    Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None, "org.freedesktop.systemd1", path, UNIT_IFACE, None)
    Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None, "org.freedesktop.systemd1", path, SVC_IFACE, None)
    both.append((time.perf_counter() - t) * 1000)
stats("UnitMonitor (Unit+Service) per UnitNew", both)

subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
