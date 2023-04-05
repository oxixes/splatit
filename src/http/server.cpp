#include "server.hpp"

HTTP_Server::HTTP_Server(Logger::Logger* logger, SettingsManager* settingsMgr, CertManager* certMgr) {
    this->logger = logger;
    this->settingsMgr = settingsMgr;
    this->certMgr = certMgr;

    server = new httplib::SSLServer(certMgr->getSSLCert(), certMgr->getSSLKey());

    server->Get("/", [](const httplib::Request &req, httplib::Response &res) {
        res.set_content("Hello World!", "text/plain");
    });
}

HTTP_Server::~HTTP_Server() {
    server->stop();
    delete server;
}

void HTTP_Server::listen() {
    server->listen("0.0.0.0", 8080);
}

void HTTP_Server::stop() {
    server->stop();
}