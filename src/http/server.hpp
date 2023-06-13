#ifndef SPLATOON_SERVER_SERVER_HPP
#define SPLATOON_SERVER_SERVER_HPP

#include "../logger.hpp"
#include "../settingsManager.hpp"
#include "../ssl/certManager.hpp"

class HTTP_Server {
public:
    HTTP_Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SettingsManager> settingsMgr,
                std::shared_ptr<CertManager> certMgr);
    ~HTTP_Server();

    void listen();
    void stop();

private:
    std::shared_ptr<SettingsManager> settingsMgr;
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<CertManager> certMgr;

    void listenTask();
};

#endif // SPLATOON_SERVER_SERVER_HPP