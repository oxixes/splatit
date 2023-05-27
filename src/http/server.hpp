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
    HTTP_Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SettingsManager> settingsMgr,
                std::shared_ptr<CertManager> certMgr);
    ~HTTP_Server();

    void listen();
    void stop();

private:
    std::unique_ptr<httplib::SSLServer> server;

    std::shared_ptr<SettingsManager> settingsMgr;
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<CertManager> certMgr;
};

#endif // SPLATOON_SERVER_SERVER_HPP