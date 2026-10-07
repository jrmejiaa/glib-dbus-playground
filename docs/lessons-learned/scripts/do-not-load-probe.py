#!/usr/bin/env python3
"""Does DO_NOT_LOAD_PROPERTIES avoid resurrecting a dead unit?

The resurrection is caused by the construction-time GetAll of a FLAGS_NONE proxy.
This builds a proxy on a settled-gone unit with DO_NOT_LOAD_PROPERTIES (no GetAll)
and checks whether UnitNew/UnitRemoved still fire. If none fire, skipping the
initial GetAll removes the side effect at the root.
"""
import subprocess
import time

from gi.repository import Gio, GLib

UNIT = "poc-dnl.service"
PATH = "/org/freedesktop/systemd1/unit/poc_2ddnl_2eservice"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe",
              None, None, Gio.DBusCallFlags.NONE, -1, None)

events = []
t0 = time.monotonic()


def stamp():
    return f"{(time.monotonic() - t0) * 1000:8.1f}ms"


def on_evt(kind):
    def h(c, s, p, i, sig, params):
        if params.unpack()[0] == UNIT:
            events.append(f"[{stamp()}] {kind}")
    return h


bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitNew", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_evt("UnitNew"))
bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitRemoved", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_evt("UnitRemoved"))

subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemd-run", "--user", f"--unit={UNIT}", "/bin/sh", "-c", "sleep 300"],
               stderr=subprocess.DEVNULL, check=False)
time.sleep(1)


def phase_stop():
    events.append(f"[{stamp()}] --- stop + settle ---")
    subprocess.run(["systemctl", "--user", "stop", UNIT], check=False)
    return False


def phase_build_dnl():
    events.append(f"[{stamp()}] >>> build proxy with DO_NOT_LOAD_PROPERTIES (no GetAll)")
    try:
        Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES, None,
                               "org.freedesktop.systemd1", PATH,
                               "org.freedesktop.systemd1.Unit", None)
        events.append(f"[{stamp()}] proxy built (no GetAll)")
    except GLib.Error as e:
        events.append(f"[{stamp()}] build failed ({e.message.split(':')[-1].strip()})")
    return False


loop = GLib.MainLoop()
GLib.timeout_add(300, phase_stop)
GLib.timeout_add(3000, phase_build_dnl)
GLib.timeout_add(5000, lambda: loop.quit() or False)
loop.run()

print("\n".join(events))
after = [e for e in events if "DO_NOT_LOAD" in e or ("UnitNew" in e and float(e.split("]")[0][1:].strip().rstrip("ms")) > 2900) or ("UnitRemoved" in e and float(e.split("]")[0][1:].strip().rstrip("ms")) > 2900)]
print("\nVERDICT:", "NO resurrection (DO_NOT_LOAD avoids it)" if not any("UnitNew" in e and float(e.split("]")[0][1:].strip().rstrip("ms")) > 3000 for e in events) else "STILL resurrects")

subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
