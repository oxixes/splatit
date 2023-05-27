#include "server.hpp"

#include <utility>

HTTP_Server::HTTP_Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SettingsManager> settingsMgr,
                         std::shared_ptr<CertManager> certMgr) {
    this->logger = std::move(logger);
    this->settingsMgr = std::move(settingsMgr);
    this->certMgr = std::move(certMgr);

    server = std::make_unique<httplib::SSLServer>(this->certMgr->getSSLCert(), this->certMgr->getSSLKey());

    server->Get("/", [](const httplib::Request &req, httplib::Response &res) {
        res.set_content("Hello World!", "text/plain");
    });
}

HTTP_Server::~HTTP_Server() {
    server->stop();
}

void HTTP_Server::listen() {
    server->listen("0.0.0.0", 8080);
}

void HTTP_Server::stop() {
    server->stop();
}