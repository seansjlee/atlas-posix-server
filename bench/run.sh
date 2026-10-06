#!/usr/bin/env bash
# usage: ./bench/run.sh [name]   -> bench/results/<name>.txt
set -euo pipefail

OUT_NAME="${1:-baseline}"
PORT=8080
WORKERS=10
DURATION=30s
THREADS=4
CONNECTIONS=100

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESULTS_DIR="$ROOT/bench/results"
mkdir -p "$RESULTS_DIR"

c++ -std=c++17 -O2 -Wall -Wextra -pthread "$ROOT/main.cpp" "$ROOT/Server.cpp" "$ROOT/Poller.cpp" -o "$ROOT/atlas_bench"

( cd "$ROOT" && ./atlas_bench "$PORT" "$WORKERS" ./public ) >/dev/null 2>&1 &
SERVER_PID=$!

cleanup() {
  kill "$SERVER_PID" 2>/dev/null || true
  rm -f "$ROOT/atlas_bench"
}
trap cleanup EXIT

sleep 2

OUT_FILE="$RESULTS_DIR/$OUT_NAME.txt"
{
  echo "Atlas benchmark — $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  echo "Host: $(uname -mrs)"
  echo "Config: workers=$WORKERS wrk-threads=$THREADS connections=$CONNECTIONS duration=$DURATION"
  echo "----------------------------------------"
  wrk -t"$THREADS" -c"$CONNECTIONS" -d"$DURATION" --latency "http://localhost:$PORT/"
} | tee "$OUT_FILE"
