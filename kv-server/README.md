# KV Server (C++ • HTTP • Cache • Persistent DB)

A multi-threaded HTTP key–value store in C++ with:

* **HTTP server** (cpp-httplib)
* **In-memory LRU cache**
* **Persistent DB** (SQLite by default; pluggable)
* **Closed-loop load generator**
* **Tiny C++ client**

Designed to demonstrate CPU- vs I/O-bound workloads, caching effectiveness, and end-to-end performance.

---

## Project Layout

```
kv-server/
├── CMakeLists.txt
├── README.md
├── config/
│   └── server_config.json
├── include/
│   ├── cache.h
│   ├── config.h
│   ├── database.h
│   ├── load_generator.h
│   ├── server.h
│   └── utils.h
├── src/
│   ├── main.cpp              # server entrypoint (HAS int main)
│   ├── server.cpp
│   ├── cache.cpp
│   ├── database.cpp
│   ├── config.cpp
│   └── utils.cpp
├── client/
│   └── client.cpp            # client entrypoint (HAS int main)
├── loadgen/
│   ├── loadgen_main.cpp      # loadgen entrypoint (HAS int main)
│   └── load_generator.cpp    # helpers (NO main)
├── tests/
│   ├── test_cache.cpp        # has its own main (or gtest_main)
│   ├── test_database.cpp     # has its own main (or gtest_main)
│   └── test_server.cpp       # has its own main (or gtest_main)
├── logs/
│   └── .gitkeep
├── scripts/
│   ├── run_server.sh
│   └── run_loadgen.sh
├── build/                    # generated
└── kv_store.db               # generated (SQLite)
```

> **Important:** exactly three entrypoints define `int main(...)`:
> `src/main.cpp`, `client/client.cpp`, `loadgen/loadgen_main.cpp`.
> Test files must have their own `main` (or link a test framework’s `*_main`).

---

## Requirements

* CMake ≥ 3.16
* C++17 compiler (GCC/Clang/MSVC)
* SQLite3 dev package

  * Ubuntu/Debian: `sudo apt-get install libsqlite3-dev`
  * macOS: `brew install sqlite`
* Internet access (CMake `FetchContent` grabs header-only deps)

Optional:

* OpenSSL (if `-DENABLE_SSL=ON`)
* PostgreSQL/MySQL dev packages (if you switch backends)

---

## Build

SQLite (default):

```bash
cmake -S . -B build -DUSE_SQLITE=ON -DBUILD_TESTS=ON
cmake --build build -j
```

Switch DB backends:

```bash
# PostgreSQL
cmake -S . -B build -DUSE_SQLITE=OFF -DUSE_POSTGRES=ON
cmake --build build -j

# MySQL
cmake -S . -B build -DUSE_SQLITE=OFF -DUSE_MYSQL=ON
cmake --build build -j
```

Sanitizers (Linux/macOS):

```bash
cmake -S . -B build -DENABLE_SANITIZERS=ON
cmake --build build -j
```

---

## Run

Start the server:

```bash
./build/kv-server
```

* Default port: `8080`
* Default DB: `kv_store.db`
* Healthcheck: `GET /health` → `OK`

Run the client:

```bash
./build/kv-client
```

Run the load generator:

```bash
./build/kv-loadgen \
  --host localhost --port 8080 \
  --clients 32 --duration 30s \
  --workload get-popular --keys 100 \
  --put-ratio 0.1 --delete-ratio 0.05
```

*(If you haven’t added CLI parsing yet, it’ll run with built-in defaults.)*

---

## API

### Create / Update

```
POST /put/{key}/{value}
```

* Writes to DB + cache
* `200 OK` on success

### Read

```
GET /get/{key}
```

* Cache first; on miss, fetch from DB and populate cache
* `200 OK` with value (text/plain), or `404 Not Found`

### Delete

```
DELETE /delete/{key}
```

* Delete from DB; evict from cache if present
* `200 OK` (even if only in DB), `404 Not Found` if absent

**cURL examples**

```bash
curl -X POST "http://localhost:8080/put/user123/hello"
curl "http://localhost:8080/get/user123"
curl -X DELETE "http://localhost:8080/delete/user123"
```

> For arbitrary payloads, add JSON endpoints later instead of path params.

---

## Configuration

`config/server_config.json`:

```json
{
  "server_port": 8080,
  "cache_size": 100,
  "database_path": "kv_store.db",
  "log_level": "INFO",
  "thread_pool_size": 8
}
```

Compile-time fallbacks (CMake options):

* `-DDEFAULT_SERVER_PORT=8080`
* `-DDEFAULT_CACHE_CAPACITY=100`

---

## Data Flow (TL;DR)

1. **Client → Server** (cpp-httplib router)
2. **Cache (LRU)**

   * GET hit → return
   * miss → **DB read**, then insert to cache (evict LRU if needed)
3. **DB**

   * POST upsert in DB, update cache
   * DELETE in DB, invalidate cache
4. **Respond** with proper status/body
5. **Loadgen** measures latency & throughput

---

## Workloads & Bottlenecks

* **I/O-bound**: `put-all`, `get-all` (many DB ops, low cache hit)
* **CPU/Mem-bound**: `get-popular` (hotset, high cache hit)
* **Mixed**: `mixed` with ratios (`--put-ratio`, `--delete-ratio`)

Use these to study:

* Cache hit rate vs latency
* Thread pool sizing
* DB tuning (WAL, sync, indexes, pooling)

---

## Testing

Build with tests:

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Test targets:

* `test-cache`       — LRU correctness
* `test-database`    — CRUD against selected backend
* `test-server`      — HTTP endpoint behaviors

> If you use a framework (e.g., GoogleTest), link `gtest_main` and drop your own mains.

---

## Troubleshooting

**`undefined reference to 'main'` during link**

* Ensure each binary includes a file **that defines `int main(...)`**:

  * `kv-server`: `src/main.cpp`
  * `kv-client`: `client/client.cpp`
  * `kv-loadgen`: `loadgen/loadgen_main.cpp`
* Don’t accidentally add `src/main.cpp` to test targets (or vice versa).

Quick check:

```bash
grep -R --line-number "int main" src client loadgen tests || echo "no mains found"
```

**SQLite not found**

* Install dev headers (`libsqlite3-dev`) and rerun CMake.
* If needed, set hint vars (e.g., `-DSQLITE3_ROOT=/path`).

**Windows link issues**

* Ensure `ws2_32` is linked (handled by CMake in this repo).

**High latency**

* Increase `thread_pool_size`
* For SQLite, enable WAL & tune `synchronous` in `database.cpp`
* Increase cache size for hot workloads

---

## Production Notes

* Prefer JSON bodies for values (encoding, size).
* Add request timeouts, rate limiting, access logs.
* DB: WAL (SQLite), prepared statements, pooling (Pg/MySQL).
* Metrics: basic `/metrics` endpoint or Prometheus integration.

---

## License

MIT (or your preference). Add a `LICENSE` file if open-sourcing.

---

### Quick Start

```bash
# build
cmake -S . -B build -DUSE_SQLITE=ON
cmake --build build -j

# run server
./build/kv-server

# try it
curl -X POST "http://localhost:8080/put/hello/world"
curl "http://localhost:8080/get/hello"
curl -X DELETE "http://localhost:8080/delete/hello"

# load test
./build/kv-loadgen --clients 32 --duration 30s --workload get-popular --keys 100
```
