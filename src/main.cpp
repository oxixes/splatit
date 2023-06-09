#include <memory>

#include "argParser.hpp"
#include "settingsManager.hpp"
#include "db/database.hpp"
#include "db/migrations/migrations.hpp"
#include "ssl/certManager.hpp"
#include "socket/socket.hpp"
#include "socket/tlsSocket.hpp"

int main(int argc, char** argv) {


    std::shared_ptr<Logger::Logger> logger(new Logger::Logger());

    // Initialize sockets (only needed on Windows)
    if(!sock::initialize()) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while initializing sockets.");
        return 1;
    }

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, logger)) return 1;

    logger->setMinLevel(serverOptions.minLogLevel);

    std::shared_ptr<SettingsManager> settingsMgr(new SettingsManager(logger));
    if (!settingsMgr->init(serverOptions)) {
        sock::cleanup();
        return 1;
    }

    std::shared_ptr<db::Database> db = db::Database::createDatabase(settingsMgr->getDBSettings(), logger);
    if (!db->init() || !db->run()) {
        db->close();
        sock::cleanup();
        return 1;
    }

    if (db->getVersion() != db::CURRENT_VERSION) {
        db::migrations::migrate(logger, db, db::DBType::SQLITE3, db->getVersion());
    }

    std::shared_ptr<CertManager> certManager(new CertManager(settingsMgr, logger));
    if (settingsMgr->isAccountEnabled() || settingsMgr->isBOSSEnabled()) {
        if (!certManager->init()) {
            certManager->cleanup();
            db->close();
            sock::cleanup();
            return 1;
        }

//        HTTP_Server httpServer = HTTP_Server(logger, settingsMgr, certManager);
//        httpServer.listen();

        //std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        //getch();

//        httpServer.stop();

        sock::TLSSocket* socket = nullptr;
        try {
            socket = new sock::TLSSocket(true, certManager->getSSLKey(), certManager->getSSLCert());
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP, "Error creating TLS socket: " + std::string(e.what()));
            return 1;
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8080);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        try {
            int opt = 1;
            socket->setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            socket->bind((struct sockaddr*)&addr, sizeof(addr));
            socket->listen();
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP, "Error setting up TLS socket: " + std::string(e.what()));
            delete socket;
            return 1;
        }

        try {
            int addrlen = sizeof(addr);
            sock::TLSSocket* client = socket->accept((struct sockaddr*)&addr,(socklen_t*)&addrlen);
            char buffer[1024] = { 0 };
            client->recv(buffer, 1024, 0);
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Received data: " + std::string(buffer));
            char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello World!";
            //char response[] = "Hello World!";
            client->send(response, strlen(response), 0);
            client->close();
            delete client;
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP, "Error accepting TLS socket: " + std::string(e.what()));
            delete socket;
            return 1;
        }

        socket->close();
        delete socket;

        certManager->cleanup();
    }

    db->close();

    // Cleanup sockets (only needed on Windows)
    sock::cleanup();

    return 0;
}
