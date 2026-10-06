#!/usr/bin/env python3
"""Experiment A for lesson 006: is the push (event) model lossless?

Question: when systemd auto-restarts a unit within milliseconds, does a
Supervisor that is briefly BUSY lose the `failed` transition and its cause?

Method: call the Manager's Subscribe() (without it systemd emits nothing),
subscribe to JobRemoved + per-unit PropertiesChanged, SIGKILL the unit's main
process, then DELIBERATELY BLOCK our own event loop for 1 second across the
entire crash+auto-restart cycle. If push is lossless, every signal emitted
during the block is queued on the connection socket and delivered, in order,
once the loop resumes.

Run it through the self-contained runner (recommended):

    ./run.sh push

or standalone against a pre-existing unit named poc-race.service:

    python3 race-probe.py block     # 'block' stalls the loop 1s; omit to stay responsive

Two gotchas this probe encodes (both produced a wrong result first):
  * You MUST call Subscribe() or systemd emits NO signals at all (ADR 0004).
    An eavesdropper like `dbus-monitor` never triggers them.
  * The unit's D-Bus object path ESCAPES the dash: `poc-race` becomes
    `poc_2drace_2eservice`. Filtering on the literal "poc-race" drops every
    PropertiesChanged. Match the escaped form.
"""
import subprocess
import sys
import time

from gi.repository import Gio, GLib

UNIT = "poc-race.service"
BLOCK = len(sys.argv) > 1 and sys.argv[1] == "block"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

# ADR 0004: without Subscribe(), systemd stays silent.
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe", None, None,
              Gio.DBusCallFlags.NONE, -1, None)

events = []
t0 = time.monotonic()


def stamp():
    return f"{(time.monotonic() - t0) * 1000:7.1f}ms"


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


def on_props(conn, sender, path, iface, signal, params):
    # NOTE the escaped path: poc-race -> poc_2drace
    if "poc_2drace" not in path:
        return
    _iface, changed, invalidated = params.unpack()
    keys = {
        k: changed[k]
        for k in ("ActiveState", "SubState", "Result", "ExecMainStatus",
                  "NRestarts") if k in changed
    }
    inv = [
        k for k in invalidated if k in ("ActiveState", "SubState", "Result",
                                        "ExecMainStatus", "NRestarts")
    ]
    if keys or inv:
        events.append(
            f"[{stamp()}] PropsChanged changed={keys} invalidated={inv}")


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
bus.signal_subscribe("org.freedesktop.systemd1",
                     "org.freedesktop.DBus.Properties", "PropertiesChanged",
                     None, None, Gio.DBusSignalFlags.NONE, on_props)

loop = GLib.MainLoop()


def kick():
    pid = subprocess.check_output(
        ["systemctl", "--user", "show", UNIT, "-p", "MainPID",
         "--value"]).decode().strip()
    events.append(f"[{stamp()}] SIGKILL {pid}" +
                  (" + BLOCK 1s" if BLOCK else ""))
    subprocess.run(["kill", "-9", pid], check=False)
    if BLOCK:
        time.sleep(1.0)  # loop processes nothing during the whole restart
        events.append(f"[{stamp()}] unblocked, draining queued signals")
    return False


def stop_test():
    loop.quit()
    return False


GLib.timeout_add(300, kick)
GLib.timeout_add(4000, stop_test)
loop.run()

print("\n".join(events))
