#!/usr/bin/env python3
"""Why does one clean stop emit a burst of UnitRemoved/UnitNew?

Isolates the cause by comparing two stop paths against the SAME transient unit,
with a passive subscriber that holds NO proxy and never calls GetUnit (so the
observer cannot itself cause reload churn):

    python3 burst-probe.py systemctl   # stop via the `systemctl stop` tool
    python3 burst-probe.py dbus        # stop via Manager.StopUnit over D-Bus

It counts UnitNew/UnitRemoved for the unit in a window after the stop. If the
`systemctl` path flaps but the `dbus` path does not, the burst is the tool
reloading the unit to report status, not systemd's stop+GC itself.
"""
import subprocess
import sys
import time

from gi.repository import Gio, GLib

UNIT = "poc-flap.service"
MODE = sys.argv[1] if len(sys.argv) > 1 else "systemctl"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe",
              None, None, Gio.DBusCallFlags.NONE, -1, None)

events = []
t0 = time.monotonic()


def stamp():
    return f"{(time.monotonic() - t0) * 1000:7.1f}ms"


def on_unit_new(conn, sender, path, iface, signal, params):
    unit, _obj = params.unpack()
    if unit == UNIT:
        events.append(f"[{stamp()}] UnitNew")


def on_unit_removed(conn, sender, path, iface, signal, params):
    unit, _obj = params.unpack()
    if unit == UNIT:
        events.append(f"[{stamp()}] UnitRemoved")


bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitNew", "/org/freedesktop/systemd1", None,
                     Gio.DBusSignalFlags.NONE, on_unit_new)
bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitRemoved", "/org/freedesktop/systemd1", None,
                     Gio.DBusSignalFlags.NONE, on_unit_removed)

loop = GLib.MainLoop()


def do_stop():
    events.append(f"[{stamp()}] stopping via {MODE}")
    if MODE == "systemctl":
        subprocess.run(["systemctl", "--user", "stop", UNIT], check=False)
    else:
        bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                      "org.freedesktop.systemd1.Manager", "StopUnit",
                      GLib.Variant("(ss)", (UNIT, "replace")), None,
                      Gio.DBusCallFlags.NONE, -1, None)
    return False


# Clean slate, start a transient unit, then stop it the chosen way.
subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemd-run", "--user", f"--unit={UNIT}", "/bin/sh", "-c", "sleep 300"],
               stderr=subprocess.DEVNULL, check=False)
time.sleep(1)

GLib.timeout_add(500, do_stop)
GLib.timeout_add(5000, lambda: loop.quit() or False)
loop.run()

print(f"=== stop path: {MODE} ===")
print("\n".join(events))
news     = sum("UnitNew" in e for e in events)
removes  = sum("UnitRemoved" in e for e in events)
print(f"\n[counts] UnitNew={news}  UnitRemoved={removes}")

subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
