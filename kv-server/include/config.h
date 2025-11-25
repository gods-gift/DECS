#pragma once
#include <string>
#include <cstddef>

struct Config {
    // Server
    int         server_port      = 8080;
    int         thread_pool_size = 8;
    std::size_t cache_size       = 20000;

    // Logging
    std::string log_level        = "INFO";

    // PostgreSQL
    std::string pg_conninfo =
        "host=127.0.0.1 port=5432 dbname=kvdb user=kvuser password=skeys";
    int         pg_pool_size     = 4;

    // Optional: CPU affinity (comma-separated CPU ids, e.g., "0-1" or "2,3")
    std::string cpu_affinity     = "";
};

/** Parse server config from command-line args (argv of kv-server). */
Config parse_server_args(int argc, char** argv, int default_port = 8080);
