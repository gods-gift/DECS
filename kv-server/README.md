# KV Server (C++): HTTP Key–Value store with Cache, DB & Load Generator

A multi-threaded HTTP key–value store with:

* **REST API** for CRUD
* **In-memory LRU cache** for hot data
* **SQLite** persistent storage (WAL, prepared statements)
* **Client CLI** and **closed-loop load generator**
* **Tests** for cache, DB, and server routes

This README is the up-to-date “how to build, run, test, and load-test” guide.

---

## 1) Requirements

Ubuntu/Debian (others similar):

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libsqlite3-dev curl
# For utilization tools (recommended for bottleneck proof)
sudo apt-get install -y sysstat
# For plotting (optional)
sudo apt-get install -y python3-matplotlib
```

---

## 2) Build

Release build (recommended for load tests):

```bash
cd ~/Desktop/DECS/kv-server
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### What gets built

* `build/kv-server` – server
* `build/kv-client` – simple client
* `build/kv-loadgen` – load generator
* Tests (when `BUILD_TESTS=ON`): `test-cache`, `test-database`, `test-server`

> We use header-only deps via CMake `FetchContent`: **cpp-httplib** and **nlohmann/json**.
> DB backend macro `DB_BACKEND_SQLITE` is enabled by default.

---

## 3) Configuration

Edit `config/server_config.json` (or set `KV_SERVER_CONFIG` env var):

```json
{
  "server_port": 8080,
  "cache_size": 20000,
  "database_path": "kv_store.db",
  "log_level": "ERROR",
  "thread_pool_size": 16
}
```

* `cache_size` controls hot-set fit (bigger → more hits/CPU-bound).
* `log_level`: `TRACE|DEBUG|INFO|WARN|ERROR|OFF`.

---

## 4) Run the server

```bash
./build/kv-server
# (or override config quickly)
./build/kv-server --port 8080 --cache-size 20000 --threads 16 --db kv_store.db
```

Health check:

```bash
curl http://localhost:8080/health
```

---

## 5) REST API

* `GET /health` → `200 OK`
* `GET /get/{key}` → `200 value` or `404`
* `POST /put/{key}/{value}` → `200` (upsert; updates DB then cache)
* `DELETE /delete/{key}` → `200` if deleted, `404` if missing

Examples:

```bash
curl -X POST "http://localhost:8080/put/user123/hello"
curl "http://localhost:8080/get/user123"
curl -X DELETE "http://localhost:8080/delete/user123"
```

---

## 6) Client CLI

From **repo root**:

```bash
./build/kv-client health
./build/kv-client put user123 hello
./build/kv-client get user123
./build/kv-client delete user123
```

---

## 7) Load Generator

Closed-loop threads; supports multiple workloads.

```
./build/kv-loadgen --host localhost --port 8080 \
  --clients 128 --duration 120s \
  --workload get-popular --keys 500
```

**Workloads**

* `put-all` – writes only (DB write-heavy, I/O-bound)
* `get-all` – reads unique keys (forced cache misses, I/O-bound)
* `get-popular` – small hot set (cache hits, CPU/memory-bound)
* `mixed` – tunable: `--put-ratio`, `--delete-ratio`, rest are GETs

Common args:

* `--clients N` concurrent threads
* `--duration 30s|300s|…`
* `--timeout-ms 3000`
* `--keys N` (size of hot set for `get-popular`/`mixed`)
* `--put-ratio`, `--delete-ratio` (for `mixed`)

Reports (stdout):

* `requests_ok`, `requests_fail`
* `throughput (req/s)`
* `avg latency (ms)`, `p50/p95/p99`

---

## 8) How it works (data flow)

* **PUT**: DB upsert (prepared statement) → update cache (LRU) → `200`.
* **GET**: check cache → hit → `200`; miss → DB select → (if found) cache insert → `200` else `404`.
* **DELETE**: DB delete authoritative → erase from cache if present → `200` or `404`.

Thread safety:

* Cache protected by a mutex (O(1) ops).
* SQLite single connection + mutex, WAL mode, `busy_timeout=5s`.

---

## 9) Proving CPU-bound vs I/O-bound (what to run & what to observe)

Open 3 terminals to **monitor** while you run loads:

**Find server PID**

```bash
SERVER_PID=$(pgrep -n kv-server); echo "$SERVER_PID"
```

**CPU per core (Terminal A)**

```bash
mpstat -P ALL 1
```

**Disk I/O (Terminal B)**

```bash
iostat -dx 1
```

**Server process stats (Terminal C)**

```bash
pidstat -r -u -d -h -p "$SERVER_PID" 1
```

### CPU-bound proof (cache-hit heavy)

Start server with a **large cache** (already done in config), then:

```bash
./build/kv-loadgen --clients 128 --duration 120s --workload get-popular --keys 500
```

Expect:

* `mpstat`: `%usr+%sys` high (busy cores), `%iowait` low
* `iostat`: disk `%util` low, `await` low
* `pidstat`: high `%CPU`, tiny `kB_rd/s` & `kB_wr/s`
* Loadgen: high throughput, low latency

### I/O-bound proof (DB-heavy)

Option A (writes):

```bash
./build/kv-loadgen --clients 64 --duration 120s --workload put-all
```

Option B (miss reads):

```bash
./build/kv-loadgen --clients 64 --duration 120s --workload get-all
```

Expect:

* `iostat`: `%util` high (70–100%), `await` higher (ms→tens of ms)
* `mpstat`: `%iowait` noticeable; CPU not saturated
* `pidstat`: large `kB_wr/s` (put-all) **or** large `kB_rd/s` (get-all)
* Loadgen: lower throughput; rising tail latencies

> Make writes more I/O-bound (optional): in `src/database.cpp`, set `PRAGMA synchronous=FULL;` and rebuild.

---

## 10) Full load-testing matrix (for your report)

Run **≥5 client levels** for **≥5 minutes** per workload.

Example:

```bash
# CPU-bound series
for C in 8 16 32 64 128; do
  ./build/kv-loadgen --clients $C --duration 300s --workload get-popular --keys 500
done

# I/O-bound series
for C in 8 16 32 64 128; do
  ./build/kv-loadgen --clients $C --duration 300s --workload put-all
done
```

Capture monitors during runs:

```bash
mkdir -p results
mpstat -P ALL 5   > results/mpstat.txt &
iostat -dx 5      > results/iostat.txt &
pidstat -r -u -d -h -p "$SERVER_PID" 5 > results/pidstat.txt &
# kill them with `kill <pid>` when done (use `jobs -l` to see pids)
```

**What to include in deliverables**

* Plots: **Throughput vs Clients** and **Avg Latency vs Clients** (per workload)
* Capacity estimate (plateau point; where latency curve bends up)
* Bottleneck analysis with evidence:

  * CPU-bound: high CPU, low disk util
  * I/O-bound: high disk util/await, moderate CPU, high rd/wr rates
* (Optional) cache hit rate if you add a `/metrics` endpoint

### Optional plotting script

If you used the CSV script earlier, you can generate graphs with:

```bash
python3 scripts/plot_results.py results/summary_*.csv
```

---

## 11) Tests

Build with tests:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build -j"$(nproc)"
cd build
ctest --output-on-failure
```

* `test-cache` – LRU behavior, eviction, erase
* `test-database` – CRUD & upsert on SQLite
* `test-server` – starts HTTP server; hits CRUD routes

---

## 12) Troubleshooting

* **`httplib.h` not found**
  Ensure CMake added its include dir; our `CMakeLists.txt` sets:

  * `FetchContent_MakeAvailable(httplib)`
  * `target_include_directories(<targets> SYSTEM PRIVATE ${httplib_SOURCE_DIR})`

* **IntelliSense shows `#error "Define one DB backend"`**
  CMake defines `DB_BACKEND_SQLITE` at build time. Point VS Code to compile commands:
  `C_Cpp.default.compileCommands = ${workspaceFolder}/build/compile_commands.json`
  Or add a fallback define for IntelliSense only.

* **Multiple `main` errors**
  Ensure only these files contain `main()`:

  * `src/main.cpp`, `client/client.cpp`, `loadgen/loadgen_main.cpp`, `tests/*.cpp` (test mains)

* **No binaries when running from `build/`**
  Use `./kv-server`, `./kv-client`, `./kv-loadgen` (drop the extra `build/` prefix if already inside `build/`).

* **Monitors missing**
  Install `sysstat`: `sudo apt-get install -y sysstat`.

---

## 13) Project layout (abridged)

```
.
├── CMakeLists.txt
├── config/
│   └── server_config.json
├── include/
│   ├── cache.h
│   ├── config.h
│   ├── database.h
│   ├── server.h
│   ├── utils.h
│   └── load_generator.h
├── src/
│   ├── cache.cpp
│   ├── config.cpp
│   ├── database.cpp
│   ├── main.cpp
│   ├── server.cpp
│   └── utils.cpp
├── client/
│   └── client.cpp
├── loadgen/
│   ├── load_generator.cpp
│   └── loadgen_main.cpp
├── tests/
│   ├── test_cache.cpp
│   ├── test_database.cpp
│   └── test_server.cpp
└── scripts/ (optional helpers)
```

---

## 14) License & notes

* The project uses **cpp-httplib** (MIT) and **nlohmann/json** (MIT).
* SQLite is included via system packages.

If you want, I can also add an optional `/metrics` endpoint for live **cache hit/miss** counters, plus a tiny script that fetches and overlays hit rate on your plots.
