#include <memory>
#include <csignal>

#include "argParser.hpp"
#include "settingsManager.hpp"
#include "crypto/certManager.hpp"
#include "db/database.hpp"
#include "db/migrations/migrations.hpp"
#include "socket/socket.hpp"
#include "socket/socketManager.hpp"
#include "http/server.hpp"
#include "http/account/account.hpp"
#include "http/boss/boss.hpp"

bool shouldStop = false;

void stop() {
    shouldStop = true;
}

void signalHandler(int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
            stop();
            break;
        default:
            break;
    }
}

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
        if (!db::migrations::migrate(logger, db, db::DBType::SQLITE3, db->getVersion())) {
            db->close();
            sock::cleanup();
            return 1;
        }
    }

    std::shared_ptr<SocketManager> socketManager(new SocketManager(logger));
    std::shared_ptr<CertManager> certManager(new CertManager(settingsMgr, logger));
    std::shared_ptr<HTTP_Server> httpServer = nullptr;

    if (settingsMgr->isAccountEnabled() || settingsMgr->isBOSSEnabled()) {
        if (!certManager->init() || (settingsMgr->isAccountEnabled() && !boss::init(logger, settingsMgr))) {
            socketManager->cleanup();
            certManager->cleanup();
            db->close();
            sock::cleanup();
            return 1;
        }

        try {
            httpServer = std::make_shared<HTTP_Server>(logger, socketManager, settingsMgr->getHTTPListenAddress(),
                                                       settingsMgr->getHTTPKeepAliveTimeout(),
                                                       certManager->getSSLKey(),
                                                       certManager->getSSLCert());
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        std::string("An error occurred while initializing the HTTP server: ") + e.what());
            socketManager->cleanup();
            certManager->cleanup();
            db->close();
            sock::cleanup();
            return 1;
        }


        if (settingsMgr->isAccountEnabled())
            acc::registerRoutes(httpServer, settingsMgr, certManager, db);

        if (settingsMgr->isBOSSEnabled())
            boss::registerRoutes(httpServer, settingsMgr);

        httpServer->listen(settingsMgr->getHTTPWorkerCount(), stop);
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    while (!shouldStop) {
        socketManager->process();
    }

    if (httpServer != nullptr) httpServer->stop();
    socketManager->cleanup();
    db->close();
    certManager->cleanup();

    // Cleanup sockets (only needed on Windows)
    sock::cleanup();

    return 0;
}
