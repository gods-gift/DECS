// loadgen/load_generator.cpp
#include "load_generator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <functional>
#include <httplib.h>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

// Simple URL-encoder for path segments (keys/values)
std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

// Percentile helper (expects v to be sorted ascending)
static double percentile_ms(const std::vector<double>& v, double p /*0..100*/) {
    const std::size_t n = v.size();
    if (n == 0) return 0.0;
    if (p <= 0.0)  return v.front();
    if (p >= 100.0) return v.back();

    const double idx = (p / 100.0) * static_cast<double>(n - 1);
    const std::size_t lo = static_cast<std::size_t>(idx);
    const std::size_t hi = std::min<std::size_t>(lo + 1, n - 1);
    const double w = idx - static_cast<double>(lo);

    return v[lo] * (1.0 - w) + v[hi] * w;
}


struct ThreadStats {
    uint64_t ok = 0;
    uint64_t fail = 0;
    std::vector<double> lat_ms; // per-request latency (ms)
    ThreadStats() { lat_ms.reserve(4096); }
};

enum class Op { GET, PUT, DEL };

struct OpSpec {
    Op op;
    std::string key;
    std::string value; // for PUT
};

// Operation generators for each workload
class OpGenerator {
public:
    OpGenerator(const Settings& s, uint32_t seed, int thread_id)
      : S(s),
        rng(seed ^ (0x9E3779B9u + static_cast<uint32_t>(thread_id) + (seed<<6) + (seed>>2))),
        pick01(0.0, 1.0),
        pick_key(0, std::max(1, S.popular_keys) - 1),
        pick_big(0, 1'000'000'000) {}

    // Unique-ish key per call
    std::string next_unique_key() {
        // large space to avoid collisions across threads
        return "k" + std::to_string(pick_big(rng));
    }

    std::string next_popular_key() {
        int k = pick_key(rng);
        return "hot" + std::to_string(k);
    }

    std::string random_value() {
        // small value payload; extend if you want to test larger bodies
        return "v" + std::to_string(pick_big(rng) & 0xFFFF);
    }

    OpSpec next_put_all() {
        return {Op::PUT, next_unique_key(), random_value()};
    }

    OpSpec next_get_all() {
        // force cache miss tendencies with unique keys
        return {Op::GET, next_unique_key(), {}};
    }

    OpSpec next_get_popular() {
        // repeatedly hit small hot set
        return {Op::GET, next_popular_key(), {}};
    }

    OpSpec next_mixed() {
        double r = pick01(rng);
        if (r < S.put_ratio) {
            return {Op::PUT, next_popular_key(), random_value()};
        } else if (r < S.put_ratio + S.delete_ratio) {
            return {Op::DEL, next_popular_key(), {}};
        } else {
            // reads dominate
            // Mix unique & popular reads to exercise both miss and hit paths
            return (pick01(rng) < 0.7)
                ? OpSpec{Op::GET, next_popular_key(), {}}
                : OpSpec{Op::GET, next_unique_key(), {}};
        }
    }

private:
    const Settings& S;
    std::mt19937_64 rng;
    std::uniform_real_distribution<double> pick01;
    std::uniform_int_distribution<int>    pick_key;
    std::uniform_int_distribution<int>    pick_big;
};

bool perform_request(httplib::Client& cli, const OpSpec& spec) {
    // Routes expected by the server:
    //   GET    /get/{key}
    //   POST   /put/{key}/{value}   (also sends body=text/plain)
    //   DELETE /delete/{key}
    switch (spec.op) {
        case Op::GET: {
            auto path = "/get/" + url_encode(spec.key);
            if (auto res = cli.Get(path.c_str())) {
                // Treat 200 and 404 as "handled"
                return res->status == 200 || res->status == 404;
            }
            return false;
        }
        case Op::PUT: {
            auto path = "/put/" + url_encode(spec.key) + "/" + url_encode(spec.value);
            if (auto res = cli.Post(path.c_str(), spec.value, "text/plain")) {
                return res->status == 200;
            }
            return false;
        }
        case Op::DEL: {
            auto path = "/delete/" + url_encode(spec.key);
            if (auto res = cli.Delete(path.c_str())) {
                // 200 or 404 both acceptable (idempotent-ish)
                return res->status == 200 || res->status == 404;
            }
            return false;
        }
    }
    return false;
}

} // namespace

Result run(const Settings& S) {
    using clock = std::chrono::steady_clock;

    const auto t_start = clock::now();
    const auto t_end   = t_start + std::chrono::seconds(std::max(1, S.duration_seconds));

    std::atomic<uint64_t> total_ok{0}, total_fail{0};
    std::mutex             lat_merge_mu;
    std::vector<double>    all_lat_ms; all_lat_ms.reserve(static_cast<size_t>(S.clients) * 2048);

    auto worker = [&](int tid) {
        ThreadStats stats;

        httplib::Client cli(S.host, S.port);
        // timeouts
        cli.set_connection_timeout(S.timeout_ms / 1000, (S.timeout_ms % 1000) * 1000);
        cli.set_read_timeout(S.timeout_ms / 1000, (S.timeout_ms % 1000) * 1000);
        cli.set_write_timeout(S.timeout_ms / 1000, (S.timeout_ms % 1000) * 1000);

        OpGenerator gen(S, S.seed, tid);

        auto pick_op = [&](WorkloadType w)->OpSpec {
            switch (w) {
                case WorkloadType::PutAll:     return gen.next_put_all();
                case WorkloadType::GetAll:     return gen.next_get_all();
                case WorkloadType::GetPopular: return gen.next_get_popular();
                case WorkloadType::Mixed:      return gen.next_mixed();
            }
            return gen.next_get_popular();
        };

        while (clock::now() < t_end) {
            OpSpec spec = pick_op(S.workload);
            auto t0 = clock::now();
            bool ok = perform_request(cli, spec);
            auto t1 = clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            if (ok) { ++stats.ok; } else { ++stats.fail; }
            stats.lat_ms.push_back(ms);
            // closed loop: send next only after response (no think time)
        }

        total_ok  += stats.ok;
        total_fail += stats.fail;

        // Merge latencies
        {
            std::lock_guard<std::mutex> lk(lat_merge_mu);
            all_lat_ms.insert(all_lat_ms.end(),
                              std::make_move_iterator(stats.lat_ms.begin()),
                              std::make_move_iterator(stats.lat_ms.end()));
        }
    };

    // Launch threads
    std::vector<std::thread> threads;
    threads.reserve(std::max(1, S.clients));
    for (int i = 0; i < std::max(1, S.clients); ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& th : threads) th.join();

    const auto elapsed_s = std::chrono::duration<double>(clock::now() - t_start).count();
    Result R{};
    R.requests_ok = total_ok.load();
    R.requests_fail = total_fail.load();

    if (!all_lat_ms.empty()) {
        // Sort for percentiles
        std::sort(all_lat_ms.begin(), all_lat_ms.end());
        // Average
        double sum = std::accumulate(all_lat_ms.begin(), all_lat_ms.end(), 0.0);
        R.avg_latency_ms = sum / static_cast<double>(all_lat_ms.size());
        R.p50_ms = percentile_ms(all_lat_ms, 50.0);
        R.p95_ms = percentile_ms(all_lat_ms, 95.0);
        R.p99_ms = percentile_ms(all_lat_ms, 99.0);
    } else {
        R.avg_latency_ms = R.p50_ms = R.p95_ms = R.p99_ms = 0.0;
    }

    const double total_requests = static_cast<double>(R.requests_ok + R.requests_fail);
    R.throughput_rps = elapsed_s > 0.0 ? (total_requests / elapsed_s) : 0.0;

    return R;
}
