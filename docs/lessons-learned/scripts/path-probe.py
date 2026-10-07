#!/usr/bin/env python3
"""Is a unit's D-Bus object path stable across unload/reload and within a flap?

The debounce design (wait out the UnitRemoved burst, act on settled state) hinges
on this: if the path is identical every time the unit loads, a cancelled teardown
leaves the monitor's proxies still valid; if it changes per load, a UnitNew in the
window must rebind, not just cancel.

Measures:
  (A) GetUnit path before stop, and after a fresh start -> same value?
  (B) the object path carried by every UnitNew/UnitRemoved during one
      `systemctl stop` burst -> all identical, or differing per load?
"""
import subprocess
import time

from gi.repository import Gio, GLib

UNIT = "poc-path.service"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe",
              None, None, Gio.DBusCallFlags.NONE, -1, None)


def get_unit_path():
    r = bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                      "org.freedesktop.systemd1.Manager", "GetUnit",
                      GLib.Variant("(s)", (UNIT,)), None, Gio.DBusCallFlags.NONE, -1, None)
    return r.unpack()[0]


def start():
    subprocess.run(["systemd-run", "--user", f"--unit={UNIT}", "/bin/sh", "-c", "sleep 300"],
                   stderr=subprocess.DEVNULL, check=False)
    time.sleep(1)


def clean():
    subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
    subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)

# (A) path stable across unload/reload?
clean()
start()
p1 = get_unit_path()
clean()
start()
p2 = get_unit_path()
print(f"(A) path before = {p1}")
print(f"(A) path after  = {p2}")
print(f"(A) STABLE across reload? {p1 == p2}\n")

# (B) paths carried within one systemctl-stop burst
burst = []


def on_new(c, s, pth, i, sig, params):
    unit, obj = params.unpack()
    if unit == UNIT:
        burst.append(("UnitNew", obj))


def on_rm(c, s, pth, i, sig, params):
    unit, obj = params.unpack()
    if unit == UNIT:
        burst.append(("UnitRemoved", obj))


bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitNew", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_new)
bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitRemoved", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_rm)

loop = GLib.MainLoop()
GLib.timeout_add(300, lambda: (subprocess.run(["systemctl", "--user", "stop", UNIT], check=False), False)[1])
GLib.timeout_add(3000, lambda: loop.quit() or False)
loop.run()

print("(B) events within one `systemctl stop` burst:")
for kind, obj in burst:
    print(f"      {kind:12} {obj}")
distinct = {obj for _, obj in burst}
print(f"(B) distinct paths in burst = {len(distinct)}")
clean()
