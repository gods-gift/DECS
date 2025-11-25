Here’s an updated `README.md` with:

* CSV columns fixed (`disk_read_MBps`, `disk_write_MBps`), and
* A new **section 10.4** explaining `mpstat`, `iostat`, and `pidstat` usage in detail.

You can paste this into `~/Desktop/DECS/kv-server/README.md`.

````markdown
# CS744 DECS KV-Server

A simple distributed key–value store for the CS744 DECS project with:

- HTTP server (`kv-server`) exposing GET/PUT/DELETE REST endpoints
- In-memory LRU cache
- Persistent PostgreSQL database backend
- Closed-loop load generator (`kv-loadgen`) for experimental workloads
- Python scripts for throughput/latency analysis and plotting

The repo is assumed to live at:

```bash
~/Desktop/DECS/kv-server
````

Adjust paths if yours is different.

---

## 1. System Architecture

### 1.1 High-level view

The system is a classic **three-tier** setup:

1. **Client / Load generator**

   * Either a human (using `curl` or `kv-client`), or the `kv-loadgen` binary.
   * Sends HTTP requests to the server.

2. **HTTP KV server**

   * Binary: `kv-server`
   * Uses `cpp-httplib` to expose REST endpoints:

     * `GET  /health`
     * `GET  /metrics`
     * `PUT  /put/<key>?value=<value>`
     * `GET  /get/<key>`
     * `DELETE /delete/<key>`
   * Orchestrates:

     * Lookups in the in-memory **LRU cache**
     * Reads/writes/deletes from/to PostgreSQL

3. **Database (PostgreSQL)**

   * Stores key–value pairs persistently in a `kv` table.
   * The server uses a small connection pool for concurrency.

### 1.2 Request path

* **PUT**

  1. Client calls `PUT /put/<key>?value=<value>`.
  2. Server writes the pair to the DB (`db_put`).
  3. Server updates the in-memory cache (`cache.put(key, value)`).
  4. Returns `200 OK`.

* **GET**

  1. Client calls `GET /get/<key>`.
  2. Server checks the cache:

     * On hit: returns cached value (`200 OK`).
     * On miss: queries DB (`db_get`).

       * If found: inserts into cache and returns `200 OK`.
       * If not found: returns `404 Not Found`.
  3. Cache hit/miss statistics are tracked.

* **DELETE**

  1. Client calls `DELETE /delete/<key>`.
  2. Server removes key in DB (`db_delete`) and cache (`cache.erase`).
  3. Returns `200 OK` if key existed, `404 Not Found` otherwise.

### 1.3 Metrics & logging

* `/metrics` returns JSON metrics:

  * `requests_total`, `errors_total`
  * `cache_hits`, `cache_misses`
  * `cache_capacity`
* Logging is handled by utilities in `utils.*`, with a global log level and optional process CPU affinity.

---

## 2. Repository Layout

At a high level:

```text
kv-server/
├── CMakeLists.txt
├── include/
│   ├── cache.h          # LRUCache class
│   ├── config.h         # Config struct and parsing
│   ├── database.h       # DB API: db_init, db_put, db_get, db_delete
│   ├── server.h         # run_server(...)
│   ├── utils.h          # logging, affinity helpers, URL encode/decode, etc.
│   └── ...
├── src/
│   ├── cache.cpp        # LRU cache implementation
│   ├── config.cpp       # parses CLI args / config file into Config
│   ├── database.cpp     # PostgreSQL connection pool and KV operations
│   ├── server.cpp       # HTTP server, handlers for /put, /get, /delete, /metrics, /health
│   ├── utils.cpp        # logging, affinity, small helpers
│   └── main.cpp         # main() entry for kv-server
├── loadgen/
│   └── load_generator.cpp  # main() for kv-loadgen; workload logic
├── tests/
│   ├── test_cache.cpp      # unit tests for LRUCache
│   ├── test_database.cpp   # DB tests (put/get/delete)
│   └── test_server.cpp     # HTTP API tests
├── csv/                 # (created by you) CSV outputs from kv-loadgen
├── plots/               # (created by you) Generated PNG plots
├── run_all_workloads.sh # helper script to run all experiments
└── plot_results.py      # Python script to generate plots
```

---

## 3. Prerequisites

### 3.1 System packages (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y \
  g++ cmake make \
  libpq-dev postgresql postgresql-contrib \
  python3 python3-pip \
  sysstat
```

(`sysstat` provides `mpstat` and `iostat`.)

### 3.2 Python packages

```bash
pip3 install pandas matplotlib
```

---

## 4. PostgreSQL Setup

Start PostgreSQL:

```bash
sudo service postgresql start
```

Create user, database, and grant privileges (match these with your `Config` defaults):

```bash
sudo -u postgres psql <<EOF
CREATE USER kvuser WITH PASSWORD 'skeys';
CREATE DATABASE kvdb OWNER kvuser;
GRANT ALL PRIVILEGES ON DATABASE kvdb TO kvuser;
EOF
```

Create the `kv` table:

```bash
psql -h 127.0.0.1 -U kvuser -d kvdb <<EOF
CREATE TABLE IF NOT EXISTS kv (
  k TEXT PRIMARY KEY,
  v TEXT
);
EOF
```

(Use `-W` if prompted for a password; the default here is `skeys`.)

---

## 5. Building the Project

From the repo root:

```bash
cd ~/Desktop/DECS/kv-server

rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Binaries (in `build/`):

* `kv-server`      – HTTP KV server
* `kv-client`      – simple CLI client
* `kv-loadgen`     – load generator
* `test-cache`     – cache unit tests
* `test-database`  – DB unit tests
* `test-server`    – server/API tests

### 5.1 Optional unit tests

```bash
cd build
./test-cache
./test-database
./test-server
```

---

## 6. Single-client Usage

### 6.1 Start the server

In one terminal:

```bash
cd ~/Desktop/DECS/kv-server/build
./kv-server --port 8080
```

### 6.2 Health check (curl)

In another terminal:

```bash
curl -v http://127.0.0.1:8080/health
# Expect: HTTP/1.1 200 OK, body: OK
```

### 6.3 Single-client PUT/GET/DELETE with curl

**PUT a value:**

```bash
curl -v -X PUT "http://127.0.0.1:8080/put/foo?value=bar"
```

**GET the same key:**

```bash
curl -v "http://127.0.0.1:8080/get/foo"
# -> HTTP/1.1 200 OK, body: bar
```

**DELETE the key:**

```bash
curl -v -X DELETE "http://127.0.0.1:8080/delete/foo"
```

**Confirm it’s gone:**

```bash
curl -v "http://127.0.0.1:8080/get/foo"
# -> HTTP/1.1 404 Not Found, body: Not found
```

### 6.4 Single-client PUT/GET/DELETE with kv-client

From the `build/` directory:

```bash
cd ~/Desktop/DECS/kv-server/build
```

**PUT a value:**

```bash
./kv-client --host 127.0.0.1 --port 8080 put foo bar
```

**GET the same key:**

```bash
./kv-client --host 127.0.0.1 --port 8080 get foo
# expected output: bar
```

**DELETE the key:**

```bash
./kv-client --host 127.0.0.1 --port 8080 delete foo
```

**Confirm it’s gone:**

```bash
./kv-client --host 127.0.0.1 --port 8080 get foo
# expected output: a "not found" message or empty result
```

> If your `kv-client` uses different flags or syntax, run `./kv-client --help` and adjust the commands accordingly.

---

## 7. CPU Pinning Layout for Experiments

To reduce noise and clearly separate bottlenecks, we pin processes to specific CPU cores:

* **Server + PostgreSQL** on cores **0,1**
* **Load generator** on cores **2,3**

### 7.1 Pin the server (cores 0–1)

```bash
cd ~/Desktop/DECS/kv-server/build
taskset -c 0-1 ./kv-server --port 8080
```

Leave this running.

### 7.2 Pin PostgreSQL (cores 0–1)

In another terminal:

```bash
for pid in $(pgrep -u postgres postgres); do
  echo "Pinning postgres PID $pid to cores 0-1"
  sudo taskset -cp 0-1 "$pid"
done
```

Now both the HTTP server and DB processes are constrained to cores 0 and 1.

---

## 8. Load Testing with kv-loadgen

We use `kv-loadgen` to generate controlled workloads and collect statistics into CSV files.

Workloads:

* `put-all`     – populates or updates the keyspace with PUTs
* `get-popular` – cache-heavy workload, repeatedly hitting a small subset of keys
* `get-all`     – DB-heavy workload, scanning a larger keyspace

### 8.1 CSV directory

From repo root:

```bash
cd ~/Desktop/DECS/kv-server
mkdir -p csv
```

### 8.2 Script: run_all_workloads.sh

Create `~/Desktop/DECS/kv-server/run_all_workloads.sh`:

```bash
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
KEYS=500

# Timings
WARMUP_PUT=0
MEASURE_PUT=60

WARMUP_GET=60
MEASURE_GET=300

# Client counts to sweep
CLIENTS=(8 16 32 64 128)

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
```

Make it executable:

```bash
cd ~/Desktop/DECS/kv-server
chmod +x run_all_workloads.sh
```

### 8.3 Run the full experiment

1. **Start server pinned to cores 0,1**

   ```bash
   cd ~/Desktop/DECS/kv-server/build
   taskset -c 0-1 ./kv-server --port 8080
   ```

2. **Pin PostgreSQL to cores 0,1** (if needed):

   ```bash
   for pid in $(pgrep -u postgres postgres); do
     sudo taskset -cp 0-1 "$pid"
   done
   ```

3. **Run pinned loadgen for all workloads (cores 2,3)**:

   ```bash
   cd ~/Desktop/DECS/kv-server
   ./run_all_workloads.sh
   ```

You should get CSV files like:

* `csv/putall_c8.csv`, `putall_c16.csv`, …, `putall_c128.csv`
* `csv/cpu_getpopular_c8.csv`, …, `cpu_getpopular_c128.csv`
* `csv/io_getall_c8.csv`, …, `io_getall_c128.csv`

Each CSV contains columns such as:

```text
timestamp,host,port,workload,clients,warmup_s,measure_s,keys,
put_ratio,delete_ratio,seed,ok,fail,thr_rps,avg_ms,p50_ms,p95_ms,p99_ms,
cpu_utilization,disk_read_MBps,disk_write_MBps
```

---

## 9. Plotting Throughput and Latency

We use a Python script to generate simple, separate plots for each workload:

* Throughput vs number of clients
* Latency (avg and p95) vs number of clients

### 9.1 plot_results.py

Create `~/Desktop/DECS/kv-server/plot_results.py`:

```python
#!/usr/bin/env python3
import argparse
import glob
import os
from typing import List

import pandas as pd
import matplotlib.pyplot as plt


def load_all_csv(csv_dir: str) -> pd.DataFrame:
    pattern = os.path.join(csv_dir, "*.csv")
    files: List[str] = glob.glob(pattern)
    if not files:
        raise SystemExit(f"No CSV files found in {csv_dir!r}")

    frames = []
    for path in files:
        try:
            df = pd.read_csv(path)
            df["source_file"] = os.path.basename(path)
            frames.append(df)
        except Exception as e:
            print(f"Warning: failed to read {path}: {e}")

    if not frames:
        raise SystemExit("No CSV files could be read successfully.")

    all_data = pd.concat(frames, ignore_index=True)

    # Drop runs with ok == 0 (failed experiments)
    if "ok" in all_data.columns:
        before = len(all_data)
        all_data = all_data[all_data["ok"] > 0].copy()
        after = len(all_data)
        if after < before:
            print(f"Filtered out {before - after} rows with ok == 0")

    return all_data


def plot_workload(df: pd.DataFrame, workload_name: str, output_dir: str) -> None:
    sub = df[df["workload"] == workload_name].copy()
    if sub.empty:
        print(f"No data for workload={workload_name!r}, skipping plots.")
        return

    if "clients" not in sub.columns or "thr_rps" not in sub.columns:
        raise SystemExit("CSV must have 'clients' and 'thr_rps' columns.")

    # Aggregate in case of multiple runs per client count
    agg = sub.groupby("clients", as_index=False).agg(
        thr_rps=("thr_rps", "mean"),
        avg_ms=("avg_ms", "mean"),
        p95_ms=("p95_ms", "mean"),
    )

    agg = agg.sort_values("clients")

    # Print table for quick inspection
    print(f"\n=== {workload_name} ===")
    print(agg[["clients", "thr_rps", "avg_ms", "p95_ms"]])

    safe_name = workload_name.replace(" ", "_")

    # Throughput vs clients
    plt.figure()
    plt.plot(agg["clients"], agg["thr_rps"], marker="o")
    plt.xlabel("Number of clients")
    plt.ylabel("Throughput (requests/second)")
    plt.title(f"Throughput vs Clients – {workload_name}")
    plt.grid(True)
    plt.tight_layout()
    out_thr = os.path.join(output_dir, f"{safe_name}_throughput.png")
    plt.savefig(out_thr)
    plt.close()
    print(f"Saved {out_thr}")

    # Latency vs clients (avg + p95)
    plt.figure()
    plt.plot(agg["clients"], agg["avg_ms"], marker="o", label="Average latency")
    plt.plot(agg["clients"], agg["p95_ms"], marker="s", label="p95 latency")
    plt.xlabel("Number of clients")
    plt.ylabel("Latency (ms)")
    plt.title(f"Latency vs Clients – {workload_name}")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    out_lat = os.path.join(output_dir, f"{safe_name}_latency.png")
    plt.savefig(out_lat)
    plt.close()
    print(f"Saved {out_lat}")
```

```python
def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot throughput and latency vs clients for DECS KV-server workloads."
    )
    parser.add_argument(
        "--csv-dir",
        default="csv",
        help="Directory containing CSV result files (default: ./csv)",
    )
    parser.add_argument(
        "--out-dir",
        default="plots",
        help="Directory to store generated PNG plots (default: ./plots)",
    )
    args = parser.parse_args()

    csv_dir = os.path.abspath(args.csv_dir)
    out_dir = os.path.abspath(args.out_dir)

    os.makedirs(out_dir, exist_ok=True)

    data = load_all_csv(csv_dir)

    workloads = ["put-all", "get-popular", "get-all"]
    for w in workloads:
        plot_workload(data, w, out_dir)


if __name__ == "__main__":
    main()
```

Make it executable (optional):

```bash
cd ~/Desktop/DECS/kv-server
chmod +x plot_results.py
```

### 9.2 Generate plots

From the repo root:

```bash
cd ~/Desktop/DECS/kv-server
mkdir -p plots
python3 plot_results.py --csv-dir csv --out-dir plots
```

This creates:

* `plots/put-all_throughput.png`
* `plots/put-all_latency.png`
* `plots/get-popular_throughput.png`
* `plots/get-popular_latency.png`
* `plots/get-all_throughput.png`
* `plots/get-all_latency.png`

---

## 10. CPU-bound and IO-bound Testing

This section explains **how to show that different workloads stress different resources**.

### 10.1 CPU-bound testing (get-popular)

Goal: show the **server CPU** is the bottleneck while disk IO is low.

1. **Workload:** `get-popular`

   * Repeatedly accesses a *small set of hot keys* (e.g., 5 hot keys within `--keys 500`).
   * After warmup, most reads hit the in-memory cache.

2. **Cache configuration:**

   * Set `cache_size` in your `Config` ≥ number of hot keys (e.g., ≥ 500).
   * This ensures hot keys stay in memory.

3. **Run with pinned cores:**

   * Server + DB on cores `0,1`.
   * Loadgen on cores `2,3` via `run_all_workloads.sh`.
   * Focus on CSVs: `csv/cpu_getpopular_c*.csv`.

4. **Observations:**

   * Throughput increases with more clients until server CPU saturates.
   * Latency grows once server CPU hits high utilization on core 0 (which `kv-loadgen` samples).
   * Disk IO (from `iostat`) remains relatively low.

5. **Optional live monitoring:** see Section **10.4** (`mpstat`, `iostat`, `pidstat`).

**Conclusion:**
`get-popular` is **CPU-bound** because the cache eliminates most disk IO, and the server CPU becomes the limiting resource.

### 10.2 IO-bound testing (get-all)

Goal: show the **database / disk IO** is the bottleneck while server CPU is not fully used.

1. **Workload:** `get-all`

   * Scans a much larger keyspace and/or more uniformly distributed keys.
   * Many requests miss the cache and go to the DB.

2. **Cache configuration:**

   * `cache_size` smaller than total keys or just rely on uniform key distribution.
   * Large fraction of requests will hit the DB.

3. **Run with pinned cores:**

   * Server + DB on cores `0,1`.
   * Loadgen on cores `2,3`.
   * Focus on CSVs: `csv/io_getall_c*.csv`.

4. **Observations:**

   * Throughput increases initially with more clients, but flattens early when DB/disk saturate.
   * Latency rises sharply as concurrency grows (queueing at the DB).
   * Disk IO and DB CPU (via `iostat` / `pidstat`) are high; server CPU may be moderate.

5. **Optional live monitoring:** see Section **10.4** (`mpstat`, `iostat`, `pidstat`).

**Conclusion:**
`get-all` is **IO-bound** because performance is limited by disk/DB throughput rather than server CPU.

### 10.3 Putting it together

Using plots from `plot_results.py`:

* **CPU-bound (get-popular):**

  * Throughput vs clients shows CPU saturation on server cores.
  * Latency vs clients stays low until CPU is maxed, then grows.
  * Disk IO is low.

* **IO-bound (get-all):**

  * Throughput vs clients flattens earlier.
  * Latency vs clients grows sharply as concurrency increases.
  * Disk IO and DB CPU are high, while server CPU may not be fully utilized.

This gives a clear story for the final writeup: two workloads, two different bottlenecks, same multi-tier system.

### 10.4 Using mpstat, iostat, and pidstat

To **cross-check** and **visualize** resource usage while `kv-loadgen` runs, you can use three standard tools:

#### 10.4.1 mpstat – per-CPU utilization

`mpstat` shows CPU usage per core.

Install (already covered via `sysstat`):

```bash
sudo apt install sysstat
```

Run:

```bash
mpstat -P ALL 1
```

* `-P ALL` = show all CPUs (core 0, 1, 2, 3, …).
* `1` = refresh every 1 second.

Look at:

* `CPU 0` and `CPU 1`: server + PostgreSQL (we pinned them there).
* `CPU 2` and `CPU 3`: load generator.

Important columns:

* `%usr` – user space usage
* `%sys` – kernel usage
* `%idle` – idle time

For **CPU-bound get-popular**:

* Expect `CPU 0` (and maybe `CPU 1`) to have high `%usr + %sys`, low `%idle`.

For **IO-bound get-all**:

* CPU might have more idle time; bottleneck will show up more in `iostat`.

#### 10.4.2 iostat – disk utilization

`iostat` shows per-disk read/write rates and utilization.

Run:

```bash
iostat -dx 1
```

* `-d` = disk-only statistics.
* `-x` = extended details (including `%util`).
* `1` = refresh every 1 second.

Key columns:

* `r/s` – read requests per second
* `w/s` – write requests per second
* `rkB/s` – read throughput (kB/s)
* `wkB/s` – write throughput (kB/s)
* `%util` – fraction of time the disk is busy

For **IO-bound get-all** and **put-all**:

* Expect high `%util` approaching 100% and high `rkB/s` / `wkB/s`.
* This confirms that the disk/DB is the limiting factor.

#### 10.4.3 pidstat – per-process usage (CPU and IO)

`pidstat` shows CPU and I/O usage per process (and per thread if requested).

**CPU usage per process:**

```bash
pidstat -u 1 -p $(pgrep kv-server)
```

* `-u` = show CPU usage.
* `1` = refresh every 1 second.
* `-p <PID>` = restrict to `kv-server` process.

Watch the `%CPU` column:

* Close to 100% (on one core) indicates a CPU-bound scenario for that process.

**Disk I/O per process:**

```bash
pidstat -d 1 -p $(pgrep -u postgres postgres | head -n 1)
```

* `-d` = I/O statistics.
* `-p` = pick one PostgreSQL backend PID.

Check:

* `kB_rd/s`, `kB_wr/s` – per-process disk read/write throughput.
* This helps show that PostgreSQL is doing a lot of IO in the IO-bound workloads.

You can mention these tools and screenshots in your report to support statements like:

* “During `get-popular`, CPU usage on core 0 is ~95% while disk `%util` is low (~5–10%).”
* “During `get-all`, CPU usage is moderate but `iostat` shows disk `%util` near 100%.”

---

## 11. Summary

This project implements:

* A RESTful KV server with HTTP + cache + DB
* A configurable, pinned-core experimental setup
* Automated scripts to collect and visualize performance metrics
* Clear CPU-bound and IO-bound workloads for analysis
* Optional live monitoring using `mpstat`, `iostat`, and `pidstat`

Follow the steps in order:

1. Set up PostgreSQL
2. Build the code
3. Test basic PUT/GET/DELETE with `curl` and `kv-client`
4. Run pinned server + DB on cores 0–1
5. Run pinned loadgen on cores 2–3 with `run_all_workloads.sh`
6. Use `plot_results.py` to generate plots
7. Use `mpstat`, `iostat`, and `pidstat` to validate CPU- vs IO-bound behavior
8. Interpret `get-popular` as CPU-bound and `get-all` as IO-bound using throughput, latency, and resource utilization

You’ll have everything you need for the DECS project report and demo.

---

## 12. License

This project is intended primarily for educational use as part of the CS744 DECS course.

Unless otherwise specified by your course or institution, you may treat this code as released under the **MIT License**:

```text
MIT License

Copyright (c) 2025 Veenu Chhabra

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

