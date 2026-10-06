#!/usr/bin/env bash
#
# Self-contained reproducer for lesson 006.
#
#   ./run.sh push     # Experiment A: queued signals survive a blocked loop
#   ./run.sh cache    # Experiment B: react to ActiveState, enrich from the cache
#
# Each run: creates a throwaway user transient unit (poc-race.service) that
# auto-restarts fast, runs the matching probe with its event loop blocked 1s
# across a crash+restart, then tears the unit down. Cleanup is guaranteed by a
# trap, even on error or Ctrl-C. Nothing touches system units; everything is in
# the per-user systemd instance.
#
# Requires: systemd (user instance running), python3 with PyGObject (gi).

set -euo pipefail

UNIT="poc-race.service"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cleanup() {
    systemctl --user stop "$UNIT" >/dev/null 2>&1 || true
    systemctl --user reset-failed "$UNIT" >/dev/null 2>&1 || true
}
trap cleanup EXIT

usage() {
    echo "usage: $0 {push|cache}" >&2
    exit 2
}

[ $# -eq 1 ] || usage
case "$1" in
    push)  PROBE="race-probe.py" ;;
    cache) PROBE="cache-probe.py" ;;
    *)     usage ;;
esac

# Start clean in case a previous run left the unit around.
cleanup

# Fast-restarting throwaway unit: a crash is auto-restarted within ~100ms, which
# is the worst case for a loop that is briefly busy.
systemd-run --user --unit="$UNIT" \
    -p Restart=on-failure -p RestartSec=100ms \
    /bin/sh -c 'sleep 300' >/dev/null 2>&1

# Give it a moment to reach active/running before we poke it.
sleep 1

echo "=== $PROBE block ==="
python3 "$SCRIPT_DIR/$PROBE" block
