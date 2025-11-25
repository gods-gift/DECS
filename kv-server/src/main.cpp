#include "config.h"
#include "server.h"
#include "utils.h"

#include <iostream>

int main(int argc, char** argv) {
    try {
        Config cfg = parse_server_args(argc, argv, 8080);
        log_set_level(cfg.log_level);

        if (!cfg.cpu_affinity.empty()) {
            std::string err;
            if (!set_process_affinity(cfg.cpu_affinity, &err)) {
                log_warn("Failed to set CPU affinity: " + err);
            } else {
                log_info("Process CPU affinity set to: " + cfg.cpu_affinity);
            }
        }

        run_server(cfg);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal exception in main: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal exception in main\n";
        return 1;
    }

    return 0;
}
