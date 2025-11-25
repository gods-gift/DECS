# KV-Server Load Testing Guide

This guide will walk you through the entire process of load testing your **KV server**. You will learn how to set up the server, run the load tests, monitor the system performance, and analyze the results step by step.

---

## **Table of Contents**

1. [Setup PostgreSQL](#1-setup-postgresql)
2. [Start the `kv-server`](#2-start-the-kv-server)
3. [Set Up the Load Generator](#3-set-up-the-load-generator)
4. [Monitor System Metrics](#4-monitor-system-metrics)
5. [Run Your Experiments](#5-run-your-experiments)
6. [Analyze Results](#6-analyze-results)
7. [Final Notes](#7-final-notes)

---

## **1. Setup PostgreSQL**

Before starting the load tests, you need a **running PostgreSQL database** for the server to interact with.

### **1.1. Start PostgreSQL Service**

Ensure PostgreSQL is installed and running on your system.

```bash
sudo systemctl start postgresql
sudo systemctl enable postgresql  # Start PostgreSQL automatically on boot
```

### **1.2. Verify PostgreSQL is Running**

Check the status of PostgreSQL:

```bash
sudo systemctl status postgresql
```

If PostgreSQL is running, you will see a message indicating it is active.

### **1.3. Check Database and Permissions**

Ensure that the `kvuser` has access to the database (`kvdb`), and the table `kv_store` exists.

```bash
sudo -u postgres psql
\c kvdb         # Switch to kvdb database
\dt             # List tables, ensure kv_store exists
```

If the table `kv_store` doesn’t exist, create it:

```sql
CREATE TABLE IF NOT EXISTS kv_store (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

---

## **2. Start the `kv-server`**

The **`kv-server`** is responsible for handling requests, interacting with the database, and providing cached responses.

### **2.1. Pin the Server to CPU Cores**

Pinning the server process to specific cores ensures it operates independently of the load generator. In **Terminal 1**, run:

```bash
cd ~/Desktop/DECS/kv-server

taskset -c 0-1 ./build/kv-server \
  --pg "host=127.0.0.1 port=5432 dbname=kvdb user=kvuser password=skeys" \
  --pg-pool 4 \
  --threads 8 \
  --cache-size 20000 \
  --log-level INFO
```

**Explanation**:

* `taskset -c 0-1`: Pins the server to cores 0 and 1.
* `--pg`: Specifies the connection details for the PostgreSQL database (`kvdb`).
* `--pg-pool 4`: Uses a connection pool of size 4.
* `--threads 8`: The server can handle up to 8 concurrent threads.
* `--cache-size 20000`: Sets the size of the in-memory cache for frequently accessed keys.

Leave this terminal open to keep the server running.

---

## **3. Set Up the Load Generator**

The **load generator** simulates multiple clients sending requests to the server.

### **3.1. Pin the Load Generator to CPU Cores**

Pinning the load generator to separate cores ensures it runs independently of the server. In **Terminal 2**, run:

```bash
cd ~/Desktop/DECS/kv-server

taskset -c 2-3 ./build/kv-loadgen \
  --host 127.0.0.1 \
  --port 8080 \
  --clients 16 \
  --warmup 60 \
  --measure 300 \
  --workload get-popular \
  --keys 500 \
  --csv results.csv
```

**Explanation**:

* `taskset -c 2-3`: Pins the load generator to cores 2 and 3.
* `--host 127.0.0.1 --port 8080`: Connects the load generator to the server running on localhost (port 8080).
* `--clients 16`: Simulates 16 concurrent clients.
* `--warmup 60`: The server will warm up for 60 seconds before the measurement phase begins.
* `--measure 300`: Measures performance for 5 minutes (300 seconds).
* `--workload get-popular`: Simulates fetching popular keys from the cache.
* `--keys 500`: Sets the number of keys in the keyspace to 500.
* `--csv results.csv`: Saves the results in `results.csv` for later analysis.

---

## **4. Monitor System Metrics**

While the server and load generator are running, you should monitor the system's performance.

### **4.1. CPU Utilization**

In **Terminal 3**, run:

```bash
mpstat -P ALL 5
```

This will show CPU usage for all cores every 5 seconds. You should observe CPU usage increasing with load, especially for **CPU-bound workloads**.

### **4.2. Disk I/O Utilization**

In **Terminal 4**, run:

```bash
iostat -dx 5
```

This will display disk I/O statistics (e.g., read/write rates, disk utilization) every 5 seconds. You should see **higher disk utilization** for **I/O-bound workloads**.

### **4.3. Server Process Stats**

In **Terminal 5**, first get the `kv-server` PID:
Let me know if you need a Python script for graphing, or any other help with your load testing process!

```bash
pgrep -n kv-server
```

Then run:

```bash
pidstat -r -u -d -h -p <PID> 5
```

Replace `<PID>` with the actual server process ID. This will display CPU, memory, and I/O stats for the `kv-server` process every 5 seconds.

---

## **5. Run Your Experiments**

You will run **multiple experiments** with **different load levels** and **two workloads**:

### **5.1. Workload 1: CPU-Bound (`get-popular`)**

The cache should handle most requests, and the system should become CPU-bound.

Run the load generator for 5 different load levels (8, 16, 32, 64, and 128 clients):

```bash
# 1) 8 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 8 --warmup 60 --measure 300 --workload get-popular --keys 500 --csv cpu_getpopular_c8.csv

# 2) 16 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 16 --warmup 60 --measure 300 --workload get-popular --keys 500 --csv cpu_getpopular_c16.csv

# 3) 32 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 32 --warmup 60 --measure 300 --workload get-popular --keys 500 --csv cpu_getpopular_c32.csv

# 4) 64 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 64 --warmup 60 --measure 300 --workload get-popular --keys 500 --csv cpu_getpopular_c64.csv

# 5) 128 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 128 --warmup 60 --measure 300 --workload get-popular --keys 500 --csv cpu_getpopular_c128.csv
```

### **5.2. Workload 2: I/O-Bound (`put-all`)**

For this workload, the system should become disk-bound due to DB writes.

Run the load generator for the same load levels:

```bash
# 1) 8 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 8 --warmup 60 --measure 300 --workload put-all --keys 200000 --csv io_putall_c8.csv

# 2) 16 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 16 --warmup 60 --measure 300 --workload put-all --keys 200000 --csv io_putall_c16.csv

# 3) 32 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 32 --warmup 60 --measure 300 --workload put-all --keys 200000 --csv io_putall_c32.csv

# 4) 64 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 64 --warmup 60 --measure 300 --workload put-all --keys 200000 --csv io_putall_c64.csv

# 5) 128 clients
./build/kv-loadgen --host 127.0.0.1 --port 8080 --clients 128 --warmup 60 --measure 300 --workload put-all --keys 200000 --csv io_putall_c128.csv
```

---

## **6. Analyze Results**

Once all the experiments have run, you’ll analyze the results:

1. **Throughput vs. Number of Clients**:

   * Plot throughput (requests/sec) on the Y-axis and the number of clients on the X-axis.
   * Expect throughput to rise initially and level off when the server reaches capacity.

2. **Latency vs. Number of Clients**:

   * Plot latency (p50, p95, p99) on the Y-axis and the number of clients on the X-axis.
   * Expect latency to increase as the system approaches saturation.

3. **CSV Analysis**:

   * Open the CSV files (`cpu_getpopular_c8.csv`, `io_putall_c128.csv`, etc.) in Excel, Google Sheets, or use a Python script to process them.
   * **Python plotting**: If you prefer Python, I can provide a script to visualize the results.

---

## **7. Final Notes**

* **Save CSV files** after each experiment.
* **Monitor system resources** closely to identify the bottleneck (CPU vs disk).
* **Use insights from system metrics** (from `mpstat`, `iostat`, and `pidstat`) to validate if the server is CPU-bound or I/O-bound during each workload.
