#!/usr/bin/env python3
"""Reproduce the UnitNew/UnitRemoved infinite loop the Supervisor hits.

Mimics the current Supervisor:
  on UnitNew (tracked, not already held): build a Unit proxy (FLAGS_NONE -> GetAll), hold it
  on UnitRemoved (held):                  drop the proxy (unref)

Modes:
  mimic   build+hold+drop a proxy like the Supervisor   (expected: loops)
  passive observe only, never build a proxy             (control: settles)

Events are capped so a real loop does not run forever. Start a transient unit,
let it settle, then stop it ONCE; watch whether UnitNew/UnitRemoved keep firing.
"""
import subprocess
import sys
import time

from gi.repository import Gio, GLib

UNIT = "poc-loop.service"
MODE = sys.argv[1] if len(sys.argv) > 1 else "mimic"
CAP = 24

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
              "org.freedesktop.systemd1.Manager", "Subscribe",
              None, None, Gio.DBusCallFlags.NONE, -1, None)

held = {}            # unit -> Gio.DBusProxy (the "monitor")
events = []
t0 = time.monotonic()
loop = GLib.MainLoop()


def stamp():
    return f"{(time.monotonic() - t0) * 1000:8.1f}ms"


def maybe_quit():
    if len(events) >= CAP:
        events.append(f"[{stamp()}] CAP reached ({CAP}) -> stopping (loop confirmed)")
        loop.quit()


def on_new(c, s, p, i, sig, params):
    unit, obj = params.unpack()
    if unit != UNIT:
        return
    events.append(f"[{stamp()}] UnitNew   held={unit in held}")
    if MODE == "mimic" and unit not in held:
        # Build a Unit proxy (FLAGS_NONE => GetAll), like the Supervisor.
        held[unit] = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                                            "org.freedesktop.systemd1", obj,
                                            "org.freedesktop.systemd1.Unit", None)
    maybe_quit()


def on_rm(c, s, p, i, sig, params):
    unit, _obj = params.unpack()
    if unit != UNIT:
        return
    events.append(f"[{stamp()}] UnitRemoved held={unit in held}")
    if MODE == "mimic" and unit in held:
        del held[unit]   # drop the proxy (unref), like erasing the UnitMonitor
    maybe_quit()


bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitNew", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_new)
bus.signal_subscribe("org.freedesktop.systemd1", "org.freedesktop.systemd1.Manager",
                     "UnitRemoved", "/org/freedesktop/systemd1", None, Gio.DBusSignalFlags.NONE, on_rm)

subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemd-run", "--user", f"--unit={UNIT}", "/bin/sh", "-c", "sleep 300"],
               stderr=subprocess.DEVNULL, check=False)
time.sleep(1)


def do_stop():
    events.append(f"[{stamp()}] --- systemctl stop (once) ---")
    subprocess.run(["systemctl", "--user", "stop", UNIT], check=False)
    return False


GLib.timeout_add(500, do_stop)
GLib.timeout_add(6000, lambda: loop.quit() or False)
loop.run()

print(f"=== mode: {MODE} ===")
print("\n".join(events))
print(f"\n[counts] UnitNew={sum('UnitNew ' in e for e in events)}  "
      f"UnitRemoved={sum('UnitRemoved' in e for e in events)}  total_events={len(events)}")

subprocess.run(["systemctl", "--user", "stop", UNIT], stderr=subprocess.DEVNULL, check=False)
subprocess.run(["systemctl", "--user", "reset-failed", UNIT], stderr=subprocess.DEVNULL, check=False)
