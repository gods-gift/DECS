// src/main.cpp
#include <iostream>
#include <string>
#include <vector>

#include "server.h"
#include "config.h"
#include "utils.h"

static void print_usage(const char* prog) {
    std::cout <<
R"(KV Server

Usage:
  )" << prog << R"( [--port N] [--cache-size N] [--threads N] [--db PATH]

Options (override config/server_config.json):
  --port N          HTTP port (default from config or compile-time)
  --cache-size N    LRU capacity (items)
  --threads N       thread pool size
  --db PATH         database path (for SQLite)
)" << std::endl;
}

int main(int argc, char** argv) {
    try {
        // 1) Load base config
        Config cfg = load_config();
        log_set_level(cfg.log_level);

        // 2) CLI overrides
        std::vector<std::string> args(argv + 1, argv + argc);
        for (size_t i = 0; i < args.size();) {
            if ((args[i] == "--port") && i + 1 < args.size()) {
                cfg.server_port = std::stoi(args[i + 1]);
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if ((args[i] == "--cache-size") && i + 1 < args.size()) {
                cfg.cache_size = std::stoi(args[i + 1]);
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if ((args[i] == "--threads") && i + 1 < args.size()) {
                cfg.thread_pool_size = std::stoi(args[i + 1]);
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if ((args[i] == "--db") && i + 1 < args.size()) {
                cfg.database_path = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if (args[i] == "-h" || args[i] == "--help") {
                print_usage(argv[0]);
                return 0;
            } else {
                std::cerr << "Unknown option: " << args[i] << "\n\n";
                print_usage(argv[0]);
                return 2;
            }
        }

        // 3) Banner
        log_info("------------------------------------------------------------");
        log_info("KV Server starting");
        log_info("  port          = " + std::to_string(cfg.server_port));
        log_info("  cache_size    = " + std::to_string(cfg.cache_size));
        log_info("  threads       = " + std::to_string(cfg.thread_pool_size));
        log_info("  database_path = " + cfg.database_path);
        log_info("  log_level     = " + cfg.log_level);
        log_info("------------------------------------------------------------");

        // 4) Run server (blocks)
        run_server(cfg);

        log_info("KV Server stopped.");
        return 0;
    } catch (const std::exception& ex) {
        log_error(std::string("Fatal: ") + ex.what());
        return 1;
    } catch (...) {
        log_error("Fatal: unknown exception");
        return 1;
    }
}
