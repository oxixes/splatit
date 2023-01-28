#ifndef SPLATOON_SERVER_SERVER_HPP
#define SPLATOON_SERVER_SERVER_HPP

#define CPPHTTPLIB_THREAD_POOL_COUNT 5
#include <httplib.h>

class HTTP_Server {
    httplib::SSLServer* server;

public:
    HTTP_Server();
    ~HTTP_Server();

    void listen();
    void stop();
};

#endif // SPLATOON_SERVER_SERVER_HPP