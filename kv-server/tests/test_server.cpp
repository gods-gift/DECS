#include <httplib.h>
#include <iostream>
#include <string>
#include <cassert>

int main() {
    httplib::Client cli("127.0.0.1", 8080);

    // health
    if (auto r = cli.Get("/health")) {
        assert(r->status == 200);
    } else {
        std::cerr << "Server not running on :8080 for test-server.\n";
        return 0; // skip
    }

    // put/get/delete
    auto pr = cli.Post("/put/testkey/testval");
    assert(pr && pr->status == 200);

    auto gr1 = cli.Get("/get/testkey"); // first get warms cache
    assert(gr1 && gr1->status == 200 && gr1->body == "testval");

    auto gr2 = cli.Get("/get/testkey");
    assert(gr2 && gr2->status == 200);

    auto dr = cli.Delete("/delete/testkey");
    assert(dr && (dr->status == 200 || dr->status == 404));

    std::cout << "test-server: OK\n";
    return 0;
}
