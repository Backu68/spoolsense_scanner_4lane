#!/usr/bin/env bash
# SpoolSense integration bench runner (#225).
#
# These tests drive a REAL scanner against a mock PrusaLink server — they are
# hardware-in-the-loop by design and cannot run in CI. This script packages
# the manual choreography: start the mock, verify the scanner can reach it,
# run the scenario scripts in order, and tear the mock down.
#
# Prerequisites (one-time, see README.md):
#   1. A scanner on the same network, with a writable NFC tag on the reader.
#   2. Scanner config: PrusaLink URL = http://<this-host-ip>:<port>.
#   3. python3 + `pip install requests`.
#
# Usage:
#   ./run_bench.sh                 # mock on port 8080, all scenarios
#   ./run_bench.sh -p 9090 e2e     # custom port, single scenario
#
# Scenarios: e2e, canceled, mismatch, xl (default: all)

set -u
cd "$(dirname "$0")"

PORT=8080
while getopts "p:" opt; do
  case $opt in
    p) PORT=$OPTARG ;;
    *) echo "usage: $0 [-p port] [e2e|canceled|mismatch|xl ...]"; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
SCENARIOS=("$@")
[ ${#SCENARIOS[@]} -eq 0 ] && SCENARIOS=(e2e canceled mismatch xl)

echo "=== SpoolSense integration bench ==="
HOST_IP=$(ipconfig getifaddr en0 2>/dev/null || hostname -I 2>/dev/null | awk '{print $1}')
echo "Mock PrusaLink: http://${HOST_IP:-<this-host>}:$PORT"
echo "Scanner must have PrusaLink URL set to that address, with a tag on the reader."
echo

python3 mock_prusalink.py --port "$PORT" &
MOCK_PID=$!
trap 'kill $MOCK_PID 2>/dev/null' EXIT
sleep 1

if ! curl -s -m 3 "http://localhost:$PORT/api/v1/status" > /dev/null; then
  echo "FATAL: mock server did not come up on port $PORT"
  exit 1
fi
echo "Mock is up (pid $MOCK_PID)."
echo "Waiting for the scanner to poll the mock (watch for its first GET)..."
echo

FAILURES=0
for s in "${SCENARIOS[@]}"; do
  case $s in
    e2e)      SCRIPT=test_print_e2e.py ;;
    canceled) SCRIPT=test_print_canceled.py ;;
    mismatch) SCRIPT=test_filament_mismatch.py ;;
    xl)       SCRIPT=test_xl_multitool.py ;;
    *) echo "unknown scenario: $s"; FAILURES=$((FAILURES+1)); continue ;;
  esac
  echo "--- scenario: $s ($SCRIPT)"
  if python3 "$SCRIPT" --mock-host localhost --mock-port "$PORT"; then
    echo "--- $s: PASS"
  else
    echo "--- $s: FAIL"
    FAILURES=$((FAILURES+1))
  fi
  echo
done

echo "=== bench done: $FAILURES failure(s) ==="
exit $((FAILURES > 0))
