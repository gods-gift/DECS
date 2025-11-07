// loadgen/loadgen_main.cpp
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include "load_generator.h"

// --- Helpers ------------------------------------------------------------

static void print_usage(const char* prog) {
    std::cout <<
R"(KV Load Generator

Usage:
  )" << prog << R"( [options]

Options:
  --host HOST              Target host (default: localhost)
  --port N                 Target port (default: 8080)
  --clients N              Concurrent clients/threads (default: 8)
  --duration D             Test duration (e.g., 30s, 2m, or plain seconds; default: 10s)
  --workload NAME          put-all | get-all | get-popular | mixed  (default: get-popular)
  --keys N                 Popular keys (hot set size) for get-popular/mixed (default: 100)
  --put-ratio R            In mixed workload, fraction of PUT ops [0..1] (default: 0.1)
  --delete-ratio R         In mixed workload, fraction of DELETE ops [0..1] (default: 0.05)
  --timeout-ms MS          Per-request timeout in milliseconds (default: 3000)
  --seed N                 RNG seed (default: 42)
  -h, --help               Show this help

Examples:
  )" << prog << R"( --clients 64 --duration 30s --workload get-popular --keys 200
  )" << prog << R"( --workload mixed --put-ratio 0.2 --delete-ratio 0.05
)";
}

static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

static bool parse_workload(const std::string& s, WorkloadType& out) {
    if (iequals(s, "put-all"))      { out = WorkloadType::PutAll; return true; }
    if (iequals(s, "get-all"))      { out = WorkloadType::GetAll; return true; }
    if (iequals(s, "get-popular"))  { out = WorkloadType::GetPopular; return true; }
    if (iequals(s, "mixed"))        { out = WorkloadType::Mixed; return true; }
    return false;
}

// Accept "30s", "2m", "120" (seconds)
static bool parse_duration_seconds(const std::string& s, int& out_seconds) {
    if (s.empty()) return false;
    char unit = 0;
    long value = 0;

    // check last char for unit
    if (std::isalpha(static_cast<unsigned char>(s.back()))) {
        unit = static_cast<char>(std::tolower(static_cast<unsigned char>(s.back())));
        try {
            value = std::stol(s.substr(0, s.size() - 1));
        } catch (...) { return false; }
    } else {
        try {
            value = std::stol(s);
        } catch (...) { return false; }
    }

    if (value <= 0) return false;

    switch (unit) {
        case 0:   out_seconds = static_cast<int>(value); break;          // plain seconds
        case 's': out_seconds = static_cast<int>(value); break;
        case 'm': out_seconds = static_cast<int>(value * 60); break;
        case 'h': out_seconds = static_cast<int>(value * 3600); break;
        default:  return false;
    }
    return true;
}

static int bad_usage(const char* prog, const std::string& msg) {
    std::cerr << "Error: " << msg << "\n\n";
    print_usage(prog);
    return 2;
}

// --- main ---------------------------------------------------------------

int main(int argc, char** argv) {
    Settings S; // defaults from header

    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size();) {
        const std::string& a = args[i];

        auto need = [&](const char* flag) {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for " << flag << "\n";
                std::exit(2);
            }
        };

        if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 0;

        } else if (a == "--host") {
            need("--host"); S.host = args[i + 1]; args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--port") {
            need("--port");
            try { S.port = std::max(1, std::stoi(args[i + 1])); }
            catch (...) { return bad_usage(argv[0], "Invalid --port value"); }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--clients") {
            need("--clients");
            try { S.clients = std::max(1, std::stoi(args[i + 1])); }
            catch (...) { return bad_usage(argv[0], "Invalid --clients value"); }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--duration") {
            need("--duration");
            int secs = 0;
            if (!parse_duration_seconds(args[i + 1], secs)) {
                return bad_usage(argv[0], "Invalid --duration (use 30s, 2m, 120, etc.)");
            }
            S.duration_seconds = secs;
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--workload") {
            need("--workload");
            if (!parse_workload(args[i + 1], S.workload)) {
                return bad_usage(argv[0], "Unknown workload. Use: put-all | get-all | get-popular | mixed");
            }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--keys") {
            need("--keys");
            try { S.popular_keys = std::max(1, std::stoi(args[i + 1])); }
            catch (...) { return bad_usage(argv[0], "Invalid --keys value"); }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--put-ratio") {
            need("--put-ratio");
            try { S.put_ratio = std::stod(args[i + 1]); }
            catch (...) { return bad_usage(argv[0], "Invalid --put-ratio value"); }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--delete-ratio") {
            need("--delete-ratio");
            try { S.delete_ratio = std::stod(args[i + 1]); }
            catch (...) { return bad_usage(argv[0], "Invalid --delete-ratio value"); }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--timeout-ms") {
            need("--timeout-ms");
            try { S.timeout_ms = std::max(1, std::stoi(args[i + 1])); }
            catch (...) { return bad_usage(argv[0], "Invalid --timeout-ms value"); }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else if (a == "--seed") {
            need("--seed");
            try { S.seed = static_cast<unsigned>(std::stoul(args[i + 1])); }
            catch (...) { return bad_usage(argv[0], "Invalid --seed value"); }
            args.erase(args.begin() + i, args.begin() + i + 2);

        } else {
            return bad_usage(argv[0], "Unknown option: " + a);
        }
    }

    // Basic validation for ratios (only used by Mixed)
    if (S.workload == WorkloadType::Mixed) {
        if (S.put_ratio < 0.0 || S.put_ratio > 1.0) {
            return bad_usage(argv[0], "--put-ratio must be in [0,1]");
        }
        if (S.delete_ratio < 0.0 || S.delete_ratio > 1.0) {
            return bad_usage(argv[0], "--delete-ratio must be in [0,1]");
        }
        if (S.put_ratio + S.delete_ratio > 1.0) {
            return bad_usage(argv[0], "--put-ratio + --delete-ratio must be <= 1.0");
        }
    }

    // Banner
    auto wl_to_str = [](WorkloadType w) {
        switch (w) {
            case WorkloadType::PutAll:     return "put-all";
            case WorkloadType::GetAll:     return "get-all";
            case WorkloadType::GetPopular: return "get-popular";
            case WorkloadType::Mixed:      return "mixed";
        }
        return "unknown";
    };

    std::cout << "KV LoadGen starting\n"
              << "  host:port       = " << S.host << ":" << S.port << "\n"
              << "  clients         = " << S.clients << "\n"
              << "  duration        = " << S.duration_seconds << "s\n"
              << "  workload        = " << wl_to_str(S.workload) << "\n"
              << "  popular_keys    = " << S.popular_keys << "\n"
              << "  put_ratio       = " << S.put_ratio << "\n"
              << "  delete_ratio    = " << S.delete_ratio << "\n"
              << "  timeout_ms      = " << S.timeout_ms << "\n"
              << "  seed            = " << S.seed << "\n"
              << "----------------------------------------------------------------\n";

    // Run
    Result R = run(S);

    // Report
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Results:\n";
    std::cout << "  requests_ok        : " << R.requests_ok << "\n";
    std::cout << "  requests_fail      : " << R.requests_fail << "\n";
    std::cout << "  throughput (req/s) : " << R.throughput_rps << "\n";
    std::cout << "  avg latency (ms)   : " << R.avg_latency_ms << "\n";
    std::cout << "  p50 / p95 / p99 ms : " << R.p50_ms << " / " << R.p95_ms << " / " << R.p99_ms << "\n";

    // Exit code: 0 if we sent at least one OK request; nonzero otherwise
    return (R.requests_ok > 0) ? 0 : 1;
}
