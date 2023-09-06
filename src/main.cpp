#include <memory>
#include <csignal>

#include "constants.hpp"
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
#include "nex/prudp/server.hpp"
#include "util/tasksManager.hpp"
#include "nex/rmc/server.hpp"
#include "nex/auth/friends_auth.hpp"

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
    std::shared_ptr<http::Server> httpServer = nullptr;

    if (settingsMgr->isAccountEnabled() || settingsMgr->isBOSSEnabled()) {
        if (!certManager->init() || (settingsMgr->isAccountEnabled() && !boss::init(logger, settingsMgr))) {
            socketManager->cleanup();
            certManager->cleanup();
            db->close();
            sock::cleanup();
            return 1;
        }

        try {
            httpServer = std::make_shared<http::Server>(logger, socketManager, settingsMgr->getHTTPListenAddress(),
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

    // FIXME: This is just for testing purposes
    std::shared_ptr<nex::prudp::Server> friendsAuthSrv = nullptr;
    sock::IPv4Addr addr {0, 0, 0, 0, 1201};
    friendsAuthSrv = std::make_shared<nex::prudp::Server>(logger, Logger::group::FRIENDS_AUTH, socketManager, addr,
                                                     0, (std::vector<uint8_t>) FRIENDS_ACCESS_KEY, true, 1,
                                                     (std::vector<uint8_t>) FRIENDS_SECURE_SERVER_KEY, true);

    auto friendsAuthRMC = std::make_shared<nex::rmc::FriendsAuthRMC>(logger);
    friendsAuthRMC->registerPRUDPServer(friendsAuthSrv, 1, 3);

    friendsAuthSrv->listen(nullptr);

//    std::shared_ptr<nex::prudp::Server> splatoonAuthSrv = nullptr;
//    addr = {0, 0, 0, 0, 1203};
//    splatoonAuthSrv = std::make_shared<nex::prudp::Server>(logger, Logger::group::SPLATOON_AUTH, socketManager, addr,
//                                                      1, (std::vector<uint8_t>) SPLATOON_ACCESS_KEY, true, 1,
//                                                      (std::vector<uint8_t>) SPLATOON_SECURE_SERVER_KEY, false);
//    splatoonAuthSrv->listen(nullptr);

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    TasksManager tasksMgr(POLL_TIMEOUT);

    while (!shouldStop) {
        tasksMgr.push(socketManager->process(tasksMgr.get()));
        tasksMgr.push(friendsAuthSrv->process());
//        tasksMgr.push(splatoonAuthSrv->process());
    }

    friendsAuthSrv->stop();

    if (httpServer != nullptr) httpServer->stop();
    socketManager->cleanup();
    db->close();
    certManager->cleanup();

    // Cleanup sockets (only needed on Windows)
    sock::cleanup();

    return 0;
}
