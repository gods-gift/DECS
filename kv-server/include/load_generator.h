#pragma once
#include <string>
#include <cstdint>

struct LoadGenConfig {
    std::string host        = "127.0.0.1";
    int         port        = 8080;

    int         clients     = 16;
    int         warmup_s    = 60;
    int         measure_s   = 300;

    std::string workload    = "get-popular"; // get-popular, get-all, put-all, mixed
    std::size_t keys        = 500;

    double      put_ratio   = 0.1;  // for mixed
    double      delete_ratio= 0.0;  // for mixed

    std::uint64_t seed      = 12345;

    std::string csv_file    = "";   // if empty, no csv written
};

int run_loadgen(const LoadGenConfig& cfg);
LoadGenConfig parse_loadgen_args(int argc, char** argv);
