#include "load_generator.h"
#include "utils.h"

#include <iostream>

int main(int argc, char** argv) {
    try {
        LoadGenConfig cfg = parse_loadgen_args(argc, argv);
        log_set_level("INFO");
        return run_loadgen(cfg);
    } catch (const std::exception& e) {
        std::cerr << "kv-loadgen fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "kv-loadgen unknown fatal error\n";
        return 1;
    }
}
