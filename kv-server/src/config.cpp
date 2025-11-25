#include "config.h"
#include "utils.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

static void apply_json(Config& cfg, const json& j) {
    if (j.contains("server_port"))      cfg.server_port      = j["server_port"].get<int>();
    if (j.contains("thread_pool_size")) cfg.thread_pool_size = j["thread_pool_size"].get<int>();
    if (j.contains("cache_size"))       cfg.cache_size       = j["cache_size"].get<std::size_t>();
    if (j.contains("log_level"))        cfg.log_level        = j["log_level"].get<std::string>();
    if (j.contains("pg_conninfo"))      cfg.pg_conninfo      = j["pg_conninfo"].get<std::string>();
    if (j.contains("pg_pool_size"))     cfg.pg_pool_size     = j["pg_pool_size"].get<int>();
    if (j.contains("cpu_affinity"))     cfg.cpu_affinity     = j["cpu_affinity"].get<std::string>();
}

Config parse_server_args(int argc, char** argv, int default_port) {
    Config cfg;
    cfg.server_port = default_port;

    // Try optional config.json
    try {
        std::ifstream in("server_config.json");
        if (in) {
            json j; in >> j;
            apply_json(cfg, j);
            log_info("Loaded server_config.json");
        }
    } catch (const std::exception& e) {
        log_warn(std::string("Failed to read server_config.json: ") + e.what());
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto next = [&](int& i) -> const char* {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for arg: " + arg);
            return argv[++i];
        };

        if (arg == "--port") {
            cfg.server_port = std::stoi(next(i));
        } else if (arg == "--threads") {
            cfg.thread_pool_size = std::stoi(next(i));
        } else if (arg == "--cache-size") {
            cfg.cache_size = static_cast<std::size_t>(std::stoll(next(i)));
        } else if (arg == "--log-level") {
            cfg.log_level = next(i);
        } else if (arg == "--pg") {
            cfg.pg_conninfo = next(i);
        } else if (arg == "--pg-pool") {
            cfg.pg_pool_size = std::stoi(next(i));
        } else if (arg == "--cpu") {
            cfg.cpu_affinity = next(i);
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "kv-server options:\n"
                << "  --port <n>          Server port (default " << cfg.server_port << ")\n"
                << "  --threads <n>       HTTP worker threads (default " << cfg.thread_pool_size << ")\n"
                << "  --cache-size <n>    Cache capacity in entries (default " << cfg.cache_size << ")\n"
                << "  --log-level <lvl>   TRACE|DEBUG|INFO|WARN|ERROR|OFF (default " << cfg.log_level << ")\n"
                << "  --pg <conninfo>     PostgreSQL conninfo string\n"
                << "  --pg-pool <n>       PostgreSQL connection pool size (default " << cfg.pg_pool_size << ")\n"
                << "  --cpu <spec>        CPU affinity (e.g. \"0-1\" or \"2,3\")\n";
            std::exit(0);
        }
    }

    return cfg;
}
