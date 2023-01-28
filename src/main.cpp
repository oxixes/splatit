#include <iostream>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_THREAD_POOL_COUNT 5
#include "httplib.h"

int main() {
    std::cout << "Hello, World!" << std::endl;

    httplib::SSLServer svr("certs\\any.nintendo.net.crt", "certs\\any.nintendo.net.key");

    svr.Get("/", [](const httplib::Request &req, httplib::Response &res) {
        std::string host = req.get_header_value("host");
        res.status = 418;
        res.set_content("Hello World!, you are coming from: " + host, "text/plain");
        std::cout << "Received request!\n";
    });

    svr.listen("0.0.0.0", 443);
    return 0;
}
