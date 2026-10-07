#!/usr/bin/env bash
#
# Fault-injection driver for the step-3 Supervisor demo.
#
#   ./fault-inject.sh <crash|stall|apperror|cleanstop> <instance-id>
#   ./fault-inject.sh demo [instance-id]      # run all four in sequence (default id 1)
#
# Each fault is injected into one running poc-dummy@<id>.service instance. Watch the
# Supervisor's log in another terminal and compare it to the "expect" line printed
# here. The daemons must already be running (the Supervisor starts them on launch,
# or: systemctl --user start poc-dummy@1.service).
#
# Faults and expected observable outcomes:
#   crash     SIGKILL main pid   -> Result=signal/core-dump -> systemd restarts
#   stall     SIGSTOP main pid   -> watchdog fires -> Result=watchdog -> systemd restarts
#   apperror  SIGUSR1 to daemon  -> State=degraded then exit(42) -> Result=exit-code -> restart
#   cleanstop systemctl stop     -> ActiveState=inactive, Result=success -> SUPERVISOR revives

set -euo pipefail

unit_of() { echo "poc-dummy@${1}.service"; }

main_pid() {
    systemctl --user show "$(unit_of "$1")" -p MainPID --value
}

inject() {
    local fault="$1" id="$2" unit pid
    unit="$(unit_of "$id")"
    pid="$(main_pid "$id")"

    case "$fault" in
        crash)
            [ "$pid" != "0" ] || { echo "no main pid for $unit"; return 1; }
            echo "[inject] crash: SIGKILL $pid ($unit)"
            echo "[expect] Result=signal (or core-dump), then systemd auto-restart"
            kill -9 "$pid"
            ;;
        stall)
            [ "$pid" != "0" ] || { echo "no main pid for $unit"; return 1; }
            echo "[inject] stall: SIGSTOP $pid ($unit)"
            echo "[expect] watchdog fires -> Result=watchdog, then systemd auto-restart"
            kill -STOP "$pid"
            ;;
        apperror)
            [ "$pid" != "0" ] || { echo "no main pid for $unit"; return 1; }
            echo "[inject] apperror: SIGUSR1 $pid ($unit)"
            echo "[expect] State=degraded (app channel) + failed/Result=exit-code (systemd channel)"
            kill -USR1 "$pid"
            ;;
        cleanstop)
            echo "[inject] cleanstop: systemctl --user stop $unit"
            echo "[expect] ActiveState=inactive/Result=success, then the Supervisor revives it"
            systemctl --user stop "$unit"
            ;;
        *)
            echo "unknown fault: $fault" >&2
            return 2
            ;;
    esac
}

if [ "${1:-}" = "demo" ]; then
    id="${2:-1}"
    for f in crash stall apperror cleanstop; do
        inject "$f" "$id"
        echo "--- waiting 8s for the cycle to settle; watch the Supervisor log ---"
        sleep 8
        systemctl --user reset-failed "$(unit_of "$id")" 2>/dev/null || true
    done
    exit 0
fi

[ $# -eq 2 ] || { grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
inject "$1" "$2"
