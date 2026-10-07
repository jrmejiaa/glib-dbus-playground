#!/usr/bin/env python3
"""Keystone check: does a crash + auto-restart emit UnitRemoved/UnitNew?

If `UnitRemoved` only ever means "cleanly gone" (never "crashed"), then reviving
on a settled UnitRemoved is safe and never fights systemd's own auto-restart.
Here we SIGKILL a Restart=on-failure unit repeatedly and count any UnitNew/
UnitRemoved for it during the auto-restart cycles.
"""
import subprocess
import time

from gi.repository import Gio, GLib

UNIT = "poc-crash.service"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe",
              None, None, Gio.DBusCallFlags.NONE, -1, None)

subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemd-run", "--user", f"--unit={UNIT}",
                "-p", "Restart=on-failure", "-p", "RestartSec=100ms",
                "/bin/sh", "-c", "sleep 300"], stderr=subprocess.DEVNULL, check=False)
time.sleep(1)

events = []


def on_new(c, s, p, i, sig, params):
    if params.unpack()[0] == UNIT:
        events.append("UnitNew")


def on_rm(c, s, p, i, sig, params):
    if params.unpack()[0] == UNIT:
        events.append("UnitRemoved")


bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitNew", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_new)
bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitRemoved", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_rm)

loop = GLib.MainLoop()
kills = {"n": 0}


def kick():
    if kills["n"] >= 5:
        return False
    pid = subprocess.check_output(["systemctl", "--user", "show", UNIT, "-p", "MainPID", "--value"]).decode().strip()
    if pid and pid != "0":
        subprocess.run(["kill", "-9", pid], check=False)
        kills["n"] += 1
    return True


GLib.timeout_add(400, kick)
GLib.timeout_add(5000, lambda: loop.quit() or False)
loop.run()

print(f"kills issued = {kills['n']}")
print(f"UnitNew during crashes     = {events.count('UnitNew')}")
print(f"UnitRemoved during crashes = {events.count('UnitRemoved')}")
print("VERDICT:", "no churn -> UnitRemoved means cleanly gone" if not events else f"CHURN: {events}")

subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
