#include "config.h"
#include "utils.h"

#include <httplib.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage:\n"
                  << "  kv-client get <key>\n"
                  << "  kv-client put <key> <value>\n"
                  << "  kv-client delete <key>\n";
        return 1;
    }

    std::string cmd = argv[1];
    std::string key = argv[2];

    std::string host = "127.0.0.1";
    int port = 8080;

    httplib::Client cli(host, port);

    if (cmd == "get") {
        auto res = cli.Get(("/get/" + url_encode(key)).c_str());
        if (!res) {
            std::cerr << "GET failed\n";
            return 1;
        }
        std::cout << "Status: " << res->status << "\n";
        std::cout << "Body  : " << res->body << "\n";
    } else if (cmd == "put") {
        if (argc < 4) {
            std::cerr << "put requires <key> <value>\n";
            return 1;
        }
        std::string value = argv[3];
        httplib::Params params;
        params.emplace("value", value);
        auto res = cli.Put(("/put/" + url_encode(key)).c_str(), params);
        if (!res) {
            std::cerr << "PUT failed\n";
            return 1;
        }
        std::cout << "Status: " << res->status << "\n";
        std::cout << "Body  : " << res->body << "\n";
    } else if (cmd == "delete") {
        auto res = cli.Delete(("/delete/" + url_encode(key)).c_str());
        if (!res) {
            std::cerr << "DELETE failed\n";
            return 1;
        }
        std::cout << "Status: " << res->status << "\n";
        std::cout << "Body  : " << res->body << "\n";
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        return 1;
    }

    return 0;
}
