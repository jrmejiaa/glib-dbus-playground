#!/usr/bin/env python3
"""Root-cause check: does accessing a settled-gone unit (GetAll via proxy build)
resurrect a UnitNew/UnitRemoved pair?

Lets a unit fully settle as gone (stop + wait for the burst to die), confirms it
is unloaded (GetUnit -> NoSuchUnit), then builds a Unit proxy (FLAGS_NONE =>
GetAll) on its path and watches whether that access alone makes systemd load it
(UnitNew) and GC it again (UnitRemoved).
"""
import subprocess
import time

from gi.repository import Gio, GLib

UNIT = "poc-access.service"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe",
              None, None, Gio.DBusCallFlags.NONE, -1, None)

events = []
t0 = time.monotonic()


def stamp():
    return f"{(time.monotonic() - t0) * 1000:8.1f}ms"


def on_new(c, s, p, i, sig, params):
    if params.unpack()[0] == UNIT:
        events.append(f"[{stamp()}] UnitNew")


def on_rm(c, s, p, i, sig, params):
    if params.unpack()[0] == UNIT:
        events.append(f"[{stamp()}] UnitRemoved")


bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitNew", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_new)
bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitRemoved", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_rm)

PATH = "/org/freedesktop/systemd1/unit/poc_2daccess_2eservice"

subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemd-run", "--user", f"--unit={UNIT}", "/bin/sh", "-c", "sleep 300"],
               stderr=subprocess.DEVNULL, check=False)
time.sleep(1)


def phase_stop():
    events.append(f"[{stamp()}] --- stop + let settle 2s ---")
    subprocess.run(["systemctl", "--user", "stop", UNIT], check=False)
    return False


def phase_confirm_gone():
    try:
        bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                      "org.freedesktop.systemd1.Manager", "GetUnit",
                      GLib.Variant("(s)", (UNIT,)), None, Gio.DBusCallFlags.NONE, -1, None)
        events.append(f"[{stamp()}] GetUnit SUCCEEDED (still loaded)")
    except GLib.Error as e:
        events.append(f"[{stamp()}] GetUnit failed -> unloaded ({e.message.split(':')[-1].strip()})")
    return False


def phase_access():
    events.append(f"[{stamp()}] >>> building Unit proxy (GetAll) on the gone unit's path")
    try:
        Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                               "org.freedesktop.systemd1", PATH,
                               "org.freedesktop.systemd1.Unit", None)
        events.append(f"[{stamp()}] proxy built (GetAll returned)")
    except GLib.Error as e:
        events.append(f"[{stamp()}] proxy build failed ({e.message.split(':')[-1].strip()})")
    return False


loop = GLib.MainLoop()
GLib.timeout_add(300, phase_stop)
GLib.timeout_add(2500, phase_confirm_gone)
GLib.timeout_add(3000, phase_access)
GLib.timeout_add(5000, lambda: loop.quit() or False)
loop.run()

print("\n".join(events))

subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
