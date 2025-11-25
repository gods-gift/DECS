#!/usr/bin/env bash
set -euo pipefail

# Paths
ROOT="$HOME/Desktop/DECS/kv-server"
BUILD="$ROOT/build"
CSV_DIR="$ROOT/csv"

mkdir -p "$CSV_DIR"
cd "$BUILD"

# Common settings
HOST=127.0.0.1
PORT=8080
KEYS=5000000

# Timings
WARMUP_PUT=60
MEASURE_PUT=300

WARMUP_GET=60
MEASURE_GET=300

# Client counts to sweep
CLIENTS=(8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768)

for c in "${CLIENTS[@]}"; do
  echo "============================"
  echo "Clients: $c"
  echo "============================"

  echo "[PUT-ALL] clients=$c"
  taskset -c 2-3 ./kv-loadgen \
    --host "$HOST" \
    --port "$PORT" \
    --workload put-all \
    --clients "$c" \
    --keys "$KEYS" \
    --warmup "$WARMUP_PUT" \
    --measure "$MEASURE_PUT" \
    --csv "$CSV_DIR/putall_c${c}.csv"

  echo "[GET-POPULAR] clients=$c"
  taskset -c 2-3 ./kv-loadgen \
    --host "$HOST" \
    --port "$PORT" \
    --workload get-popular \
    --clients "$c" \
    --keys "$KEYS" \
    --warmup "$WARMUP_GET" \
    --measure "$MEASURE_GET" \
    --csv "$CSV_DIR/cpu_getpopular_c${c}.csv"

  echo "[GET-ALL] clients=$c"
  taskset -c 2-3 ./kv-loadgen \
    --host "$HOST" \
    --port "$PORT" \
    --workload get-all \
    --clients "$c" \
    --keys "$KEYS" \
    --warmup "$WARMUP_GET" \
    --measure "$MEASURE_GET" \
    --csv "$CSV_DIR/io_getall_c${c}.csv"

  echo
done

echo "All runs completed. CSVs are in: $CSV_DIR"
