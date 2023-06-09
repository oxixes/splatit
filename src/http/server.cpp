#include "server.hpp"

#include <utility>

HTTP_Server::HTTP_Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SettingsManager> settingsMgr,
                         std::shared_ptr<CertManager> certMgr) {
    this->logger = std::move(logger);
    this->settingsMgr = std::move(settingsMgr);
    this->certMgr = std::move(certMgr);



//
//    server = std::make_unique<httplib::SSLServer>(this->certMgr->getSSLCert(), this->certMgr->getSSLKey());
//
//    //server->new_task_queue = [] { return new httplib::ThreadPool(5); };
//
//    server->Get("/", [](const httplib::Request &req, httplib::Response &res) {
//        res.set_content("Hello World!", "text/plain");
//    });
//
//    server->Get("/stop", [&](const httplib::Request &req, httplib::Response &res) {
//        try {
//            server->stop();
//        } catch (const std::exception &e) {
//            this->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Error stopping HTTP server: " + std::string(e.what()));
//        }
//    });
}

HTTP_Server::~HTTP_Server() {
//    stop();
}

void HTTP_Server::listen() {
//    logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "Starting HTTP server...");
//    serverRunThread = std::thread(&HTTP_Server::listenTask, this);
}

void HTTP_Server::listenTask() {
//    server->listen("0.0.0.0", 8080);
}

void HTTP_Server::stop() {
    //server->stop();
    //serverRunThread.join();
}