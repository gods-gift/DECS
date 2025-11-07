// client/client.cpp
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cctype>
#include <httplib.h>

struct Options {
    std::string host = "localhost";
    int port = 8080;
    int retries = 2;               // extra attempts after the first try
    int timeout_ms = 3000;         // per request timeout
};

static void print_usage(const char* prog) {
    std::cout <<
R"(KV Client

Usage:
  )" << prog << R"( [--host HOST] [--port PORT] [--retries N] [--timeout-ms MS] <command> [args...]

Commands:
  get <key>
  put <key> <value>
  delete <key>
  del <key>                 (alias for delete)

Examples:
  )" << prog << R"( get user123
  )" << prog << R"( put user123 hello
  )" << prog << R"( delete user123
)";
}

static std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        // Unreserved characters: A-Z a-z 0-9 - _ . ~
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

static bool do_get(httplib::Client& cli, const std::string& key) {
    auto path = "/get/" + url_encode(key);
    if (auto res = cli.Get(path.c_str())) {
        std::cout << "[GET] " << path << " -> HTTP " << res->status << "\n";
        if (res->status == 200) {
            std::cout << res->body << "\n";
            return true;
        } else {
            std::cerr << res->body << "\n";
            return res->status == 404 ? true : false; // treat 404 as a handled response
        }
    } else {
        std::cerr << "[GET] request failed (network/timeout)\n";
        return false;
    }
}

static bool do_put(httplib::Client& cli, const std::string& key, const std::string& value) {
    auto path = "/put/" + url_encode(key) + "/" + url_encode(value);
    if (auto res = cli.Post(path.c_str(), value, "text/plain")) {
        std::cout << "[POST] " << path << " -> HTTP " << res->status << "\n";
        if (res->status == 200) {
            std::cout << res->body << "\n";
            return true;
        } else {
            std::cerr << res->body << "\n";
            return false;
        }
    } else {
        std::cerr << "[POST] request failed (network/timeout)\n";
        return false;
    }
}

static bool do_delete(httplib::Client& cli, const std::string& key) {
    auto path = "/delete/" + url_encode(key);
    if (auto res = cli.Delete(path.c_str())) {
        std::cout << "[DELETE] " << path << " -> HTTP " << res->status << "\n";
        if (res->status == 200) {
            std::cout << res->body << "\n";
            return true;
        } else {
            std::cerr << res->body << "\n";
            return res->status == 404 ? true : false; // treat 404 as a handled response
        }
    } else {
        std::cerr << "[DELETE] request failed (network/timeout)\n";
        return false;
    }
}

int main(int argc, char** argv) {
    Options opt;

    // Basic CLI parsing
    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size();) {
        if (args[i] == "--host" && i + 1 < args.size()) {
            opt.host = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--port" && i + 1 < args.size()) {
            opt.port = std::stoi(args[i + 1]);
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--retries" && i + 1 < args.size()) {
            opt.retries = std::max(0, std::stoi(args[i + 1]));
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--timeout-ms" && i + 1 < args.size()) {
            opt.timeout_ms = std::max(1, std::stoi(args[i + 1]));
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else {
            ++i; // non-option, move on
        }
    }

    if (args.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string cmd = args[0];

    // Prepare client
    httplib::Client cli(opt.host, opt.port);
    cli.set_connection_timeout(opt.timeout_ms / 1000, (opt.timeout_ms % 1000) * 1000);
    cli.set_read_timeout(opt.timeout_ms / 1000, (opt.timeout_ms % 1000) * 1000);
    cli.set_write_timeout(opt.timeout_ms / 1000, (opt.timeout_ms % 1000) * 1000);

    auto attempt = [&](auto&& fn) {
        int tries_left = opt.retries + 1;
        while (tries_left--) {
            if (fn()) return true;
            if (tries_left > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                std::cerr << "Retrying...\n";
            }
        }
        return false;
    };

    if (cmd == "get") {
        if (args.size() != 2) {
            std::cerr << "Usage: " << argv[0] << " get <key>\n";
            return 1;
        }
        const std::string key = args[1];
        return attempt([&] { return do_get(cli, key); }) ? 0 : 2;

    } else if (cmd == "put") {
        if (args.size() != 3) {
            std::cerr << "Usage: " << argv[0] << " put <key> <value>\n";
            return 1;
        }
        const std::string key = args[1];
        const std::string value = args[2];
        return attempt([&] { return do_put(cli, key, value); }) ? 0 : 2;

    } else if (cmd == "delete" || cmd == "del") {
        if (args.size() != 2) {
            std::cerr << "Usage: " << argv[0] << " delete <key>\n";
            return 1;
        }
        const std::string key = args[1];
        return attempt([&] { return do_delete(cli, key); }) ? 0 : 2;

    } else if (cmd == "health") {
        // Simple health check to help during setup
        if (auto res = cli.Get("/health"); res && res->status == 200) {
            std::cout << res->body << "\n";
            return 0;
        } else {
            std::cerr << "Health check failed\n";
            return 2;
        }
    } else {
        std::cerr << "Unknown command: " << cmd << "\n\n";
        print_usage(argv[0]);
        return 1;
    }
}
