#ifndef SPLATOON_SERVER_SERVER_HPP
#define SPLATOON_SERVER_SERVER_HPP

#include "../logger.hpp"
#include "../settingsManager.hpp"
#include "../ssl/certManager.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_THREAD_POOL_COUNT 5
#include <httplib.h>

class HTTP_Server {
public:
    HTTP_Server(Logger::Logger* logger, SettingsManager* settingsMgr, CertManager* certMgr);
    ~HTTP_Server();

    void listen();
    void stop();

private:
    httplib::SSLServer* server;

    SettingsManager* settingsMgr;
    Logger::Logger* logger;
    CertManager* certMgr;
};

#endif // SPLATOON_SERVER_SERVER_HPP