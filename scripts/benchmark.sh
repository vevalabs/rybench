#!/usr/bin/env bash
# Throughput benchmark for a RyBench transport.
# Runs the publisher at max rate for a fixed window (stdout redirected so terminal
# I/O does not throttle the loop), then reports total messages and msg/s.
#
# Usage: scripts/benchmark.sh [binary] [node] [seconds]
# Defaults: rybench-iceoryx2  filewriter:perf@  10
#
# Examples:
#   scripts/benchmark.sh                                   # iceoryx2, perf node, 10s
#   scripts/benchmark.sh rybench-filewriter filewriter:perf@ 5
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${1:-rybench-iceoryx2}"
NODE="${2:-filewriter:perf@}"
SECONDS_RUN="${3:-10}"

BUILD_DIR=build_release
EXE="$BUILD_DIR/bin/$BIN"
LOG="$(mktemp /tmp/rybench-bench-XXXX.log)"

if [[ ! -x "$EXE" ]]; then
    echo "Missing $EXE — build first: scripts/build.sh $BIN" >&2
    exit 1
fi

export RYBENCH_CONFIG=config

echo "Running $BIN ($NODE) for ${SECONDS_RUN}s at max rate..."
timeout -s INT "$SECONDS_RUN" "$EXE" "$NODE" >"$LOG" 2>&1 || true

TOTAL="$(grep -oiP 'total messages:\s*\K[0-9]+' "$LOG" | tail -1 || true)"

if [[ -z "${TOTAL:-}" ]]; then
    echo "No total found. Last log lines:" >&2
    tail -n 20 "$LOG" >&2
    exit 1
fi

RATE=$(( TOTAL / SECONDS_RUN ))
printf 'Total messages: %s\n' "$TOTAL"
printf 'Throughput:     %s msg/s\n' "$RATE"
printf 'Est. time for 1,000,000 msgs: %.2f s\n' "$(echo "1000000 / $RATE" | bc -l)"
echo "Full log: $LOG"
