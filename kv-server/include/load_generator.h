// include/load_generator.h
#pragma once

#include <cstdint>
#include <string>

/** Workload shapes the request mix produced by each client thread. */
enum class WorkloadType {
  PutAll,     // Only PUTs (DB-heavy, I/O-bound)
  GetAll,     // Only GETs with unique keys (forced cache misses, I/O-bound)
  GetPopular, // GETs on a small hot set (high cache hit rate, CPU/mem-bound)
  Mixed       // Mix of GET/PUT/DELETE via ratios below
};

/** Tunables for the load generator. */
struct Settings {
  std::string host = "localhost";
  int         port = 8080;

  int         clients = 8;             // concurrent threads
  int         duration_seconds = 10;   // total run time

  WorkloadType workload = WorkloadType::GetPopular;

  // For GetPopular/Mixed workloads:
  int         popular_keys = 100;      // size of hot key set

  // Only used by Mixed workload:
  double      put_ratio = 0.10;        // [0..1], fraction of PUT
  double      delete_ratio = 0.05;     // [0..1], fraction of DELETE (GET fills the rest)

  int         timeout_ms = 3000;       // per-request timeout
  unsigned    seed = 42;               // RNG seed
};

/** Aggregate results returned by the load generator. */
struct Result {
  std::uint64_t requests_ok   = 0;     // responses considered successful/handled
  std::uint64_t requests_fail = 0;     // network/timeouts or unexpected statuses

  double avg_latency_ms = 0.0;         // arithmetic mean over all requests
  double p50_ms = 0.0;                 // latency percentiles (ms)
  double p95_ms = 0.0;
  double p99_ms = 0.0;

  double throughput_rps = 0.0;         // total (ok+fail) / elapsed seconds
};

/**
 * Run the closed-loop load generator with the given settings.
 * Spawns `clients` threads, each issuing a request and waiting for the response
 * before issuing the next (no think time). Returns aggregate metrics.
 */
Result run(const Settings& s);
