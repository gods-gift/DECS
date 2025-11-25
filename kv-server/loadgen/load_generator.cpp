#include "load_generator.h"
#include "utils.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace {

double pctl(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = (p / 100.0) * static_cast<double>(v.size() - 1);
    std::size_t lo = static_cast<std::size_t>(idx);
    std::size_t hi = std::min(lo + 1, v.size() - 1);
    double w = idx - static_cast<double>(lo);
    return v[lo] * (1.0 - w) + v[hi] * w;
}

enum class Op { GET, PUT, DEL };

// ---------- CPU & Disk sampling helpers (Linux /proc-based) ----------

struct CpuSample {
    unsigned long long user   = 0;
    unsigned long long nice   = 0;
    unsigned long long system = 0;
    unsigned long long idle   = 0;
    unsigned long long iowait = 0;
    unsigned long long irq    = 0;
    unsigned long long softirq= 0;
    unsigned long long steal  = 0;
};

// Read stats for *core 0 only* (line "cpu0" in /proc/stat)
bool read_cpu_sample(CpuSample& s) {
    std::ifstream f("/proc/stat");
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string label;
        iss >> label;

        if (label == "cpu0") {
            iss >> s.user >> s.nice >> s.system >> s.idle >> s.iowait
                >> s.irq >> s.softirq >> s.steal;
            return true;
        }
    }
    return false;
}

double cpu_utilization(const CpuSample& a, const CpuSample& b) {
    auto idle_a  = a.idle + a.iowait;
    auto idle_b  = b.idle + b.iowait;
    auto total_a = a.user + a.nice + a.system + a.idle + a.iowait +
                   a.irq + a.softirq + a.steal;
    auto total_b = b.user + b.nice + b.system + b.idle + b.iowait +
                   b.irq + b.softirq + b.steal;

    double totald = static_cast<double>(total_b - total_a);
    double idled  = static_cast<double>(idle_b - idle_a);
    if (totald <= 0.0) return 0.0;
    return 100.0 * (1.0 - idled / totald);
}

struct DiskSample {
    unsigned long long read_sectors  = 0;
    unsigned long long write_sectors = 0;
};

// Aggregate sectors read/written across all non-loop, non-ram devices
bool read_disk_sample(DiskSample& s) {
    std::ifstream f("/proc/diskstats");
    if (!f) return false;

    std::string line;
    unsigned long long total_read = 0;
    unsigned long long total_write = 0;

    while (std::getline(f, line)) {
        std::istringstream iss(line);
        unsigned long long major = 0, minor = 0;
        std::string name;
        if (!(iss >> major >> minor >> name)) continue;

        // Skip loopback / ram devices
        if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0) continue;

        unsigned long long rd_ios = 0, rd_merges = 0, rd_sectors = 0, rd_ticks = 0;
        unsigned long long wr_ios = 0, wr_merges = 0, wr_sectors = 0, wr_ticks = 0;

        if (!(iss >> rd_ios >> rd_merges >> rd_sectors >> rd_ticks
                  >> wr_ios >> wr_merges >> wr_sectors >> wr_ticks)) {
            continue;
        }

        total_read  += rd_sectors;
        total_write += wr_sectors;
    }

    s.read_sectors  = total_read;
    s.write_sectors = total_write;
    return true;
}

void compute_disk_rates(const DiskSample& a, const DiskSample& b,
                        double seconds,
                        double& read_MBps,
                        double& write_MBps) {
    if (seconds <= 0.0) {
        read_MBps = write_MBps = 0.0;
        return;
    }

    constexpr double sector_size = 512.0; // bytes
    double read_bytes  = static_cast<double>(b.read_sectors  - a.read_sectors)  * sector_size;
    double write_bytes = static_cast<double>(b.write_sectors - a.write_sectors) * sector_size;

    read_MBps  = (read_bytes  / (1024.0 * 1024.0)) / seconds;
    write_MBps = (write_bytes / (1024.0 * 1024.0)) / seconds;

    if (read_MBps  < 0.0) read_MBps  = 0.0;
    if (write_MBps < 0.0) write_MBps = 0.0;
}

} // namespace

LoadGenConfig parse_loadgen_args(int argc, char** argv) {
    LoadGenConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](int& i) -> const char* {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            return argv[++i];
        };

        if (arg == "--host")        cfg.host      = next(i);
        else if (arg == "--port")   cfg.port      = std::stoi(next(i));
        else if (arg == "--clients")cfg.clients   = std::stoi(next(i));
        else if (arg == "--warmup") cfg.warmup_s  = std::stoi(next(i));
        else if (arg == "--measure")cfg.measure_s = std::stoi(next(i));
        else if (arg == "--workload")cfg.workload = next(i);
        else if (arg == "--keys")   cfg.keys      = static_cast<std::size_t>(std::stoull(next(i)));
        else if (arg == "--put-ratio") cfg.put_ratio = std::stod(next(i));
        else if (arg == "--delete-ratio") cfg.delete_ratio = std::stod(next(i));
        else if (arg == "--seed")   cfg.seed      = std::stoull(next(i));
        else if (arg == "--csv")    cfg.csv_file  = next(i);
        else if (arg == "--help" || arg == "-h") {
            std::cout
                << "kv-loadgen options:\n"
                << "  --host <ip>           Server host (default 127.0.0.1)\n"
                << "  --port <n>            Server port (default 8080)\n"
                << "  --clients <n>         Number of client threads\n"
                << "  --warmup <s>          Warmup seconds (not measured)\n"
                << "  --measure <s>         Measurement seconds\n"
                << "  --workload <type>     get-popular|get-all|put-all|mixed\n"
                << "  --keys <n>            Number of distinct keys\n"
                << "  --put-ratio <r>       PUT ratio for mixed (0..1)\n"
                << "  --delete-ratio <r>    DELETE ratio for mixed (0..1)\n"
                << "  --seed <n>            RNG seed\n"
                << "  --csv <file>          Write summary CSV row\n";
            std::exit(0);
        }
    }

    return cfg;
}

int run_loadgen(const LoadGenConfig& cfg) {
    log_info("Loadgen connecting to " + cfg.host + ":" + std::to_string(cfg.port) +
             " workload=" + cfg.workload +
             " clients=" + std::to_string(cfg.clients));

    std::atomic<uint64_t> ok{0};
    std::atomic<uint64_t> fail{0};
    std::mutex lat_mu;
    std::vector<double> lat_ms;

    auto start_all = std::chrono::steady_clock::now();
    auto warmup_end = start_all + std::chrono::seconds(cfg.warmup_s);
    auto measure_end = warmup_end + std::chrono::seconds(cfg.measure_s);

    // Samples for CPU and disk over the measurement window (warmup excluded)
    CpuSample cpu_before{}, cpu_after{};
    DiskSample disk_before{}, disk_after{};
    bool have_cpu_samples  = false;
    bool have_disk_samples = false;

    auto worker = [&](int id) {
        httplib::Client cli(cfg.host, cfg.port);
        cli.set_keep_alive(true);

        std::mt19937_64 rng(cfg.seed + static_cast<std::uint64_t>(id));
        if (cfg.keys == 0) {
            // Avoid UB if someone misconfigures keys=0
            return;
        }
        std::uniform_int_distribution<std::uint64_t> keydist(0, cfg.keys - 1);
        std::uniform_real_distribution<double> u01(0.0, 1.0);

        // For get-popular: define a hot subset
        const double hot_prob = 0.9; // 90% of requests go to hot set
        std::size_t hot_count = std::min<std::size_t>(5, cfg.keys); // up to 5 hot keys
        std::uniform_int_distribution<std::uint64_t> hotdist(
            0, hot_count > 0 ? static_cast<std::uint64_t>(hot_count - 1) : 0
        );
        // Cold range: [hot_count, cfg.keys-1] if there is any cold key
        std::uniform_int_distribution<std::uint64_t> colddist(
            hot_count, cfg.keys > hot_count ? static_cast<std::uint64_t>(cfg.keys - 1)
                                            : static_cast<std::uint64_t>(hot_count)
        );

        while (std::chrono::steady_clock::now() < measure_end) {
            Op op = Op::GET;

            if (cfg.workload == "get-popular") {
                op = Op::GET;
            } else if (cfg.workload == "get-all") {
                op = Op::GET;
            } else if (cfg.workload == "put-all") {
                op = Op::PUT;
            } else if (cfg.workload == "mixed") {
                double r = u01(rng);
                if (r < cfg.put_ratio) op = Op::PUT;
                else if (r < cfg.put_ratio + cfg.delete_ratio) op = Op::DEL;
                else op = Op::GET;
            }

            // --- Key selection logic ---
            uint64_t key_index = 0;

            if (cfg.workload == "get-popular") {
                // 90% of requests go to the first 'hot_count' keys,
                // remaining 10% spread across the rest of the keyspace.
                if (cfg.keys <= hot_count) {
                    // All keys are "hot" if total keys <= hot_count
                    key_index = hotdist(rng);
                } else {
                    double r = u01(rng);
                    if (r < hot_prob) {
                        key_index = hotdist(rng);     // pick from hot subset
                    } else {
                        key_index = colddist(rng);    // pick from cold keys
                    }
                }
            } else {
                // Other workloads: uniform over full key range
                key_index = keydist(rng);
            }

            std::string key = "key" + std::to_string(key_index);

            auto t0 = std::chrono::steady_clock::now();

            bool success = false;

            if (op == Op::GET) {
                auto res = cli.Get(("/get/" + url_encode(key)).c_str());
                success = (res && res->status == 200);
            } else if (op == Op::PUT) {
                httplib::Params p;
                p.emplace("value", "v" + std::to_string(id));
                auto res = cli.Put(("/put/" + url_encode(key)).c_str(), p);
                success = (res && res->status == 200);
            } else { // DEL
                auto res = cli.Delete(("/delete/" + url_encode(key)).c_str());
                success = (res && (res->status == 200 || res->status == 404));
            }

            auto t1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            auto now = std::chrono::steady_clock::now();
            if (now > warmup_end && now <= measure_end) {
                if (success) ok.fetch_add(1, std::memory_order_relaxed);
                else         fail.fetch_add(1, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lk(lat_mu);
                lat_ms.push_back(ms);
            }
        }
    };

    // Background sampler: measure CPU and disk during the *measurement* window only
    std::thread sampler([&]() {
        // Wait until warmup is done
        std::this_thread::sleep_until(warmup_end);

        have_cpu_samples  = read_cpu_sample(cpu_before);
        have_disk_samples = read_disk_sample(disk_before);

        // Wait until measurement is done
        std::this_thread::sleep_until(measure_end);

        if (have_cpu_samples) {
            have_cpu_samples = read_cpu_sample(cpu_after);
        }
        if (have_disk_samples) {
            have_disk_samples = read_disk_sample(disk_after);
        }
    });

    std::vector<std::thread> threads;
    threads.reserve(cfg.clients);
    for (int i = 0; i < cfg.clients; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) t.join();

    auto end_all = std::chrono::steady_clock::now();
    (void)end_all; // currently unused

    if (sampler.joinable()) {
        sampler.join();
    }

    double measure_seconds = static_cast<double>(cfg.measure_s);
    double thr = measure_seconds > 0.0 ? static_cast<double>(ok.load()) / measure_seconds : 0.0;

    double avg = 0.0;
    if (!lat_ms.empty()) {
        double sum = 0.0;
        for (double x : lat_ms) sum += x;
        avg = sum / static_cast<double>(lat_ms.size());
    }
    double p50 = pctl(lat_ms, 50.0);
    double p95 = pctl(lat_ms, 95.0);
    double p99 = pctl(lat_ms, 99.0);

    double cpu_util = 0.0;
    double disk_read_MBps = 0.0;
    double disk_write_MBps = 0.0;

    if (have_cpu_samples) {
        cpu_util = cpu_utilization(cpu_before, cpu_after);
    }
    if (have_disk_samples) {
        compute_disk_rates(disk_before, disk_after, measure_seconds,
                           disk_read_MBps, disk_write_MBps);
    }

    std::cout << "Loadgen summary:\n"
              << "  ok=" << ok.load() << " fail=" << fail.load() << "\n"
              << "  throughput=" << thr << " req/s\n"
              << "  avg=" << avg << "ms p50=" << p50
              << "ms p95=" << p95 << "ms p99=" << p99 << "ms\n"
              << "  cpu_util=" << cpu_util << "%\n"
              << "  disk_read=" << disk_read_MBps << " MB/s"
              << " disk_write=" << disk_write_MBps << " MB/s\n";

    if (!cfg.csv_file.empty()) {
        bool exists = false;
        {
            std::ifstream in(cfg.csv_file);
            exists = in.good();
        }
        std::ofstream out(cfg.csv_file, std::ios::app);
        if (!out) {
            std::cerr << "Failed to open CSV file: " << cfg.csv_file << "\n";
        } else {
            if (!exists) {
                out << "timestamp,host,port,workload,clients,warmup_s,measure_s,keys,"
                       "put_ratio,delete_ratio,seed,ok,fail,thr_rps,avg_ms,p50_ms,p95_ms,p99_ms,"
                       "cpu_utilization,disk_read_MBps,disk_write_MBps\n";
            }
            auto ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            out << ts << ","
                << cfg.host << ","
                << cfg.port << ","
                << cfg.workload << ","
                << cfg.clients << ","
                << cfg.warmup_s << ","
                << cfg.measure_s << ","
                << cfg.keys << ","
                << cfg.put_ratio << ","
                << cfg.delete_ratio << ","
                << cfg.seed << ","
                << ok.load() << ","
                << fail.load() << ","
                << thr << ","
                << avg << ","
                << p50 << ","
                << p95 << ","
                << p99 << ","
                << cpu_util << ","
                << disk_read_MBps << ","
                << disk_write_MBps << "\n";
        }
    }

    return 0;
}
