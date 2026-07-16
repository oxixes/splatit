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
#include "grpc/server.hpp"
#include "http/account/account.hpp"
#include "http/boss/boss.hpp"
#include "nex/prudp/server.hpp"
#include "util/tasksManager.hpp"
#include "nex/auth/auth.hpp"
#include "nex/friends/friendsSecure.hpp"
#include "nex/splatoon/splatoonSecure.hpp"
#include "http/management/management.hpp"
#include "util/globalTaskScheduler.hpp"
#include "grpc/channelPool.hpp"
#include "sharedState/localSharedState.hpp"
#include "sharedState/redisSharedState.hpp"

std::atomic<bool> shouldStop = false;

void stop() {
    shouldStop = true;
}

void signalHandler(const int signal) {
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
    std::shared_ptr<Logger::Logger> logger = std::make_shared<Logger::Logger>();

    // Initialize sockets (only needed on Windows)
    if (!sock::initialize()) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while initializing sockets.");
        return 1;
    }

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, logger)) return 1;

    logger->setMinLevel(serverOptions.minLogLevel);

    std::shared_ptr<SettingsManager> settingsMgr(new SettingsManager(logger));
    std::shared_ptr<crypto::CertManager> certManager(new crypto::CertManager(settingsMgr, logger));
    if (!settingsMgr->init(serverOptions) || !certManager->init()) {
        sock::cleanup();
        return 1;
    }

    std::shared_ptr<grpc::ChannelCredentials> grpcCredentials = nullptr;
    if (settingsMgr->isgRPCTlsEnabled()) {
        try {
            grpcCredentials = grpcimpl::createTlsChannelCredentials(
                settingsMgr->getgRPCTlsCertPath(),
                settingsMgr->getgRPCTlsKeyPath(),
                settingsMgr->getgRPCTlsCaCertPath());
            logger->log(Logger::level::INFO, Logger::group::SETUP, "gRPC TLS credentials loaded");
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        std::string("Failed to load gRPC TLS credentials: ") + e.what());
            sock::cleanup();
            return 1;
        }
    }

    std::shared_ptr<SocketManager> socketManager(new SocketManager(logger));

    std::shared_ptr<db::Database> friendsAuthDB = nullptr;
    std::shared_ptr<nex::prudp::Server> friendsAuthSrv = nullptr;
    std::shared_ptr<nex::rmc::AuthRMC> friendsAuthRMC;
    if (settingsMgr->isFriendsAuthEnabled()) {
        friendsAuthDB = db::Database::createDatabase(settingsMgr->getFriendsAuthDBSettings(), logger);
        if (!friendsAuthDB->init() || !friendsAuthDB->run()) {
            friendsAuthDB->close();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }

        if (friendsAuthDB->getVersion() != db::CURRENT_VERSION) {
            if (!db::migrations::migrate(logger, friendsAuthDB, db::DBType::SQLITE3, db::SystemType::FRIENDS_AUTH,
                                         friendsAuthDB->getVersion())) {
                friendsAuthDB->close();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }
        }

        sock::IPv4Addr addr = settingsMgr->getFriendsAuthListenAddress();

        friendsAuthSrv = std::make_shared<nex::prudp::Server>(logger, Logger::group::FRIENDS_AUTH, socketManager, settingsMgr,
                                                              addr, 0, FRIENDS_ACCESS_KEY,
                                                              true, 1, std::vector<uint8_t>(), true);

        sock::IPv4Addr secureAddr = settingsMgr->getFriendsSecureServerAddress();
        friendsAuthRMC = std::make_shared<nex::rmc::AuthRMC>(logger, Logger::group::FRIENDS_AUTH, friendsAuthDB,
                                                             secureAddr, FRIENDS_SERVER_ID,
                                                             FRIENDS_SECURE_SERVER_KEY,
                                                             FRIENDS_SERVER_BUILD, "", true, settingsMgr->getNEXServerID());
        friendsAuthRMC->registerPRUDPServer(friendsAuthSrv, 1, settingsMgr->getFriendsAuthWorkerCount());

        friendsAuthSrv->listen(stop);
    }

    // Initialize SharedState if friendsSecure or splatoonSecure are enabled
    std::shared_ptr<ss::SharedState> sharedState = nullptr;
    if (settingsMgr->isFriendsSecureEnabled() || settingsMgr->isSplatoonSecureEnabled()) {
        std::string sharedStateType = settingsMgr->getSharedStateType();

        if (sharedStateType == "redis") {
            json redisSettings = settingsMgr->getSharedStateRedisSettings();

            ss::RedisConfig redisConfig;
            redisConfig.host = redisSettings["host"].get<std::string>();
            redisConfig.port = redisSettings["port"].get<uint16_t>();
            redisConfig.password = redisSettings.value("password", "");
            redisConfig.database = redisSettings["database"].get<int>();
            redisConfig.connectionTimeoutMs = redisSettings["connectionTimeoutMs"].get<uint32_t>();
            redisConfig.commandTimeoutMs = redisSettings["commandTimeoutMs"].get<uint32_t>();
            redisConfig.useSSL = redisSettings["useSSL"].get<bool>();
            redisConfig.caCertPath = redisSettings.value("caCertPath", "");
            redisConfig.certPath = redisSettings.value("certPath", "");
            redisConfig.keyPath = redisSettings.value("keyPath", "");
            redisConfig.workerThreads = redisSettings["workerThreads"].get<uint32_t>();
            redisConfig.clientTTLSeconds = redisSettings["clientTTLSeconds"].get<uint32_t>();

            sharedState = std::make_shared<ss::RedisSharedState>(logger, redisConfig, settingsMgr->getNEXServerID(), settingsMgr->getgRCPPublicFacingAddress());

            if (!sharedState->init()) {
                logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                            "Failed to initialize Redis SharedState");
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }

            logger->log(Logger::level::INFO, Logger::group::SETUP,
                        "Redis SharedState initialized successfully");
        } else {
            // Default to local
            sharedState = std::make_shared<ss::LocalSharedState>(logger, settingsMgr->getNEXServerID(), settingsMgr->getgRCPPublicFacingAddress());

            if (!sharedState->init()) {
                logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                            "Failed to initialize Local SharedState");
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }

            logger->log(Logger::level::INFO, Logger::group::SETUP,
                        "Local SharedState initialized successfully");
        }
    }

    std::shared_ptr<db::Database> friendsSecureDB = nullptr;
    std::shared_ptr<nex::prudp::Server> friendsSecureSrv = nullptr;
    std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsSecureRMC;
    if (settingsMgr->isFriendsSecureEnabled()) {
        friendsSecureDB = db::Database::createDatabase(settingsMgr->getFriendsSecureDBSettings(), logger);
        if (!friendsSecureDB->init() || !friendsSecureDB->run()) {
            friendsSecureDB->close();
            if (sharedState != nullptr) sharedState->close();
            if (friendsAuthDB != nullptr) friendsAuthDB->close();
            if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }

        if (friendsSecureDB->getVersion() != db::CURRENT_VERSION) {
            if (!db::migrations::migrate(logger, friendsSecureDB, db::DBType::SQLITE3, db::SystemType::FRIENDS_SECURE,
                                         friendsSecureDB->getVersion())) {
                friendsSecureDB->close();
                if (sharedState != nullptr) sharedState->close();
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }
        }

        sock::IPv4Addr addr = settingsMgr->getFriendsSecureListenAddress();
        friendsSecureSrv = std::make_shared<nex::prudp::Server>(logger, Logger::group::FRIENDS_SECURE, socketManager, settingsMgr,
                                                                addr, 0, FRIENDS_ACCESS_KEY,
                                                                false, 2, FRIENDS_SECURE_SERVER_KEY,
                                                                true);

        friendsSecureRMC = std::make_shared<nex::rmc::FriendsSecureRMC>(logger, friendsSecureDB,
                                                                         settingsMgr->getNEXTokenKey(), sharedState, settingsMgr->getNEXServerID(),
                                                                         settingsMgr->getFriendsSecuregRPCConnectionPoolMaxSize(),
                                                                         settingsMgr->getFriendsSecuregRPCRequestTimeout(),
                                                                         grpcCredentials);
        friendsSecureRMC->registerPRUDPServer(friendsSecureSrv, 1, settingsMgr->getFriendsSecureWorkerCount());

        friendsSecureSrv->listen(stop);
    }

    std::shared_ptr<db::Database> splatoonAuthDB = nullptr;
    std::shared_ptr<nex::prudp::Server> splatoonAuthSrv = nullptr;
    std::shared_ptr<nex::rmc::AuthRMC> splatoonAuthRMC;
    if (settingsMgr->isSplatoonAuthEnabled()) {
        splatoonAuthDB = db::Database::createDatabase(settingsMgr->getSplatoonAuthDBSettings(), logger);
        if (!splatoonAuthDB->init() || !splatoonAuthDB->run()) {
            splatoonAuthDB->close();
            if (sharedState != nullptr) sharedState->close();
            if (friendsSecureDB != nullptr) friendsSecureDB->close();
            if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
            if (friendsAuthDB != nullptr) friendsAuthDB->close();
            if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }

        if (splatoonAuthDB->getVersion() != db::CURRENT_VERSION) {
            if (!db::migrations::migrate(logger, splatoonAuthDB, db::DBType::SQLITE3, db::SystemType::SPLATOON_AUTH,
                                         splatoonAuthDB->getVersion())) {
                splatoonAuthDB->close();
                if (sharedState != nullptr) sharedState->close();
                if (friendsSecureDB != nullptr) friendsSecureDB->close();
                if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }
        }

        sock::IPv4Addr addr = settingsMgr->getSplatoonAuthListenAddress();
        splatoonAuthSrv = std::make_shared<nex::prudp::Server>(logger, Logger::group::SPLATOON_AUTH, socketManager, settingsMgr,
                                                               addr, 1, SPLATOON_ACCESS_KEY,
                                                               true, 1, std::vector<uint8_t>(),
                                                               false);

        sock::IPv4Addr secureAddr = settingsMgr->getSplatoonSecureServerAddress();
        splatoonAuthRMC = std::make_shared<nex::rmc::AuthRMC>(logger, Logger::group::SPLATOON_AUTH, splatoonAuthDB,
                                                             secureAddr, SPLATOON_SERVER_ID,
                                                             SPLATOON_SECURE_SERVER_KEY,
                                                             SPLATOON_SERVER_BUILD, settingsMgr->getNEXTokenKey(), false, settingsMgr->getNEXServerID());
        splatoonAuthRMC->registerPRUDPServer(splatoonAuthSrv, 1, settingsMgr->getSplatoonAuthWorkerCount());

        splatoonAuthSrv->listen(stop);
    }

    std::shared_ptr<nex::prudp::Server> splatoonSecureSrv = nullptr;
    std::shared_ptr<db::Database> splatoonSecureDB = nullptr;
    std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonSecureRMC;
    if (settingsMgr->isSplatoonSecureEnabled()) {
        splatoonSecureDB = db::Database::createDatabase(settingsMgr->getSplatoonSecureDBSettings(), logger);
        if (!splatoonSecureDB->init() || !splatoonSecureDB->run()) {
            splatoonSecureDB->close();
            if (sharedState != nullptr) sharedState->close();
            if (friendsSecureDB != nullptr) friendsSecureDB->close();
            if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
            if (friendsAuthDB != nullptr) friendsAuthDB->close();
            if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
            if (splatoonAuthDB != nullptr) splatoonAuthDB->close();
            if (splatoonAuthSrv != nullptr) splatoonAuthSrv->stop();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }

        if (splatoonSecureDB->getVersion() != db::CURRENT_VERSION) {
            if (!db::migrations::migrate(logger, splatoonSecureDB, db::DBType::SQLITE3, db::SystemType::SPLATOON_SECURE,
                                         splatoonSecureDB->getVersion())) {
                splatoonSecureDB->close();
                if (sharedState != nullptr) sharedState->close();
                if (friendsSecureDB != nullptr) friendsSecureDB->close();
                if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                if (splatoonAuthDB != nullptr) splatoonAuthDB->close();
                if (splatoonAuthSrv != nullptr) splatoonAuthSrv->stop();
                if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }
        }

        sock::IPv4Addr addr = settingsMgr->getSplatoonSecureListenAddress();
        splatoonSecureSrv = std::make_shared<nex::prudp::Server>(logger, Logger::group::SPLATOON_SECURE, socketManager, settingsMgr,
                                                                  addr, 1, SPLATOON_ACCESS_KEY,
                                                                  false, 2, SPLATOON_SECURE_SERVER_KEY,
                                                                  false);

        splatoonSecureRMC = std::make_shared<nex::rmc::SplatoonSecureRMC>(logger, splatoonSecureDB, sharedState, settingsMgr->getNEXServerID(),
            settingsMgr->getSplatoonSecuregRPCConnectionPoolMaxSize(), settingsMgr->getSplatoonSecuregRPCRequestTimeout(),
            grpcCredentials);
        splatoonSecureRMC->registerPRUDPServer(splatoonSecureSrv, 1, settingsMgr->getSplatoonSecureWorkerCount());

        splatoonSecureSrv->listen(stop);
    }

    std::shared_ptr<db::Database> accountsDB = nullptr;
    std::shared_ptr<db::Database> bossDB = nullptr;
    std::shared_ptr<db::Database> managementDB = nullptr;
    std::shared_ptr<http::Server> httpServer = nullptr;
    std::shared_ptr<http::Server> managementServer = nullptr;

    if (settingsMgr->isAccountEnabled() || settingsMgr->isBOSSEnabled()) {
        if (settingsMgr->isBOSSEnabled()) {
            bossDB = db::Database::createDatabase(settingsMgr->getBOSSDBSettings(), logger);
            if (!bossDB->init() || !bossDB->run()) {
                bossDB->close();
                if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
                if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
                if (sharedState != nullptr) sharedState->close();
                if (friendsSecureDB != nullptr) friendsSecureDB->close();
                if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }

            if (bossDB->getVersion() != db::CURRENT_VERSION) {
                if (!db::migrations::migrate(logger, bossDB, db::DBType::SQLITE3, db::SystemType::BOSS, bossDB->getVersion())) {
                    bossDB->close();
                    if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
                    if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
                    if (sharedState != nullptr) sharedState->close();
                    if (friendsSecureDB != nullptr) friendsSecureDB->close();
                    if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                    if (friendsAuthDB != nullptr) friendsAuthDB->close();
                    if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                    socketManager->cleanup();
                    certManager->cleanup();
                    sock::cleanup();
                    return 1;
                }
            }
        }

        try {
            httpServer = std::make_shared<http::Server>(logger, socketManager, settingsMgr->getHTTPListenAddress(),
                                                        settingsMgr->getHTTPKeepAliveTimeout(),
                                                        settingsMgr->isHTTP_SSL_Enabled(),
                                                        certManager->getSSLKey(),
                                                        certManager->getSSLCert());
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        std::string("An error occurred while initializing the HTTP server: ") + e.what());
            if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
            if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
            if (sharedState != nullptr) sharedState->close();
            if (friendsSecureDB != nullptr) friendsSecureDB->close();
            if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
            if (friendsAuthDB != nullptr) friendsAuthDB->close();
            if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }

        if (settingsMgr->isAccountEnabled()) {
            accountsDB = db::Database::createDatabase(settingsMgr->getAccountsDBSettings(), logger);
            if (!accountsDB->init() || !accountsDB->run()) {
                accountsDB->close();
                httpServer->stop();
                if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
                if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
                if (sharedState != nullptr) sharedState->close();
                if (friendsSecureDB != nullptr) friendsSecureDB->close();
                if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }

            if (accountsDB->getVersion() != db::CURRENT_VERSION) {
                if (!db::migrations::migrate(logger, accountsDB, db::DBType::SQLITE3, db::SystemType::ACCOUNTS,
                                             accountsDB->getVersion())) {
                    accountsDB->close();
                    httpServer->stop();
                    if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
                    if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
                    if (sharedState != nullptr) sharedState->close();
                    if (friendsSecureDB != nullptr) friendsSecureDB->close();
                    if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                    if (friendsAuthDB != nullptr) friendsAuthDB->close();
                    if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                    socketManager->cleanup();
                    certManager->cleanup();
                    sock::cleanup();
                    return 1;
                }
            }

            if (!acc::init(logger)) {
                accountsDB->close();
                httpServer->stop();
                if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
                if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
                if (friendsSecureDB != nullptr) friendsSecureDB->close();
                if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }

            acc::registerRoutes(httpServer, settingsMgr, certManager, accountsDB, grpcCredentials);
        }

        if (settingsMgr->isBOSSEnabled())
            boss::registerRoutes(httpServer, settingsMgr, bossDB);

        httpServer->listen(settingsMgr->getHTTPWorkerCount(), stop);
    }

    if (settingsMgr->isManagementEnabled()) {
        // Create and migrate the management database
        managementDB = db::Database::createDatabase(settingsMgr->getManagementDBSettings(), logger);
        if (!managementDB->init() || !managementDB->run()) {
            managementDB->close();
            managementDB = nullptr;
            logger->log(Logger::level::FAILURE, Logger::group::SETUP, "Failed to initialize management database");
            if (accountsDB != nullptr) accountsDB->close();
            if (httpServer != nullptr) httpServer->stop();
            if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
            if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
            if (friendsSecureDB != nullptr) friendsSecureDB->close();
            if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
            if (friendsAuthDB != nullptr) friendsAuthDB->close();
            if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }

        if (managementDB->getVersion() != db::CURRENT_VERSION) {
            if (!db::migrations::migrate(logger, managementDB, db::DBType::SQLITE3, db::SystemType::MANAGEMENT,
                                         managementDB->getVersion())) {
                managementDB->close();
                managementDB = nullptr;
                if (accountsDB != nullptr) accountsDB->close();
                if (httpServer != nullptr) httpServer->stop();
                if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
                if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
                if (friendsSecureDB != nullptr) friendsSecureDB->close();
                if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
                if (friendsAuthDB != nullptr) friendsAuthDB->close();
                if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
                socketManager->cleanup();
                certManager->cleanup();
                sock::cleanup();
                return 1;
            }
        }

        try {
            managementServer = std::make_shared<http::Server>(logger, socketManager, settingsMgr->getManagementListenAddress(),
                                                              settingsMgr->getManagementKeepAliveTimeout(),
                                                              false);
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        std::string("An error occurred while initializing the Management server: ") + e.what());
            if (accountsDB != nullptr) accountsDB->close();
            if (httpServer != nullptr) httpServer->stop();
            if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
            if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
            if (friendsSecureDB != nullptr) friendsSecureDB->close();
            if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
            if (friendsAuthDB != nullptr) friendsAuthDB->close();
            if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }

        mgm::registerRoutes(managementServer, settingsMgr, managementDB, logger, grpcCredentials);
        managementServer->listen(settingsMgr->getManagementWorkerCount(), stop);
    }

    std::shared_ptr<grpcimpl::Server> grpcServer = nullptr;

    if (settingsMgr->isgRPCEnabled()) {
        try {
            grpcimpl::gRPCServerData serverPtrs {
                settingsMgr,
                certManager,
                accountsDB,
                bossDB,
                httpServer,
                friendsAuthRMC,
                splatoonAuthRMC,
                friendsSecureRMC,
                splatoonSecureRMC
            };

            grpcServer = std::make_shared<grpcimpl::Server>(logger, settingsMgr->getgRPCListenAddress(),
                                                            settingsMgr->isgRPCReflectionEnabled(),
                                                            serverPtrs,
                                                            settingsMgr->isgRPCTlsEnabled(),
                                                            settingsMgr->getgRPCTlsCertPath(),
                                                            settingsMgr->getgRPCTlsKeyPath(),
                                                            settingsMgr->getgRPCTlsCaCertPath());
            grpcServer->listen();
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        std::string("An error occurred while initializing the gRPC server: ") + e.what());
            if (accountsDB != nullptr) accountsDB->close();
            if (httpServer != nullptr) httpServer->stop();
            if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
            if (splatoonSecureDB != nullptr) splatoonSecureDB->close();
            if (friendsSecureDB != nullptr) friendsSecureDB->close();
            if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
            if (friendsAuthDB != nullptr) friendsAuthDB->close();
            if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
            socketManager->cleanup();
            certManager->cleanup();
            sock::cleanup();
            return 1;
        }
    }

    // Create a channel pool for the global task scheduler to make boss gRPC calls
    auto taskSchedulerChannelPool = std::make_shared<grpcimpl::ChannelPool>(10, grpcCredentials);
    util::GlobalTaskScheduler::createInstance(accountsDB, logger, settingsMgr, managementDB, taskSchedulerChannelPool);

#ifdef _WIN32
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#else
    struct sigaction sigIntHandler{};
    sigIntHandler.sa_handler = signalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sigIntHandler, nullptr);
    sigaction(SIGTERM, &sigIntHandler, nullptr);
    sigIntHandler.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sigIntHandler, nullptr);
#endif

    TasksManager tasksMgr(POLL_TIMEOUT);

    while (!shouldStop) {
        tasksMgr.push(socketManager->process(tasksMgr.get()));
        if (sharedState != nullptr) tasksMgr.push(sharedState->process());
        if (friendsAuthSrv != nullptr) tasksMgr.push(friendsAuthSrv->process());
        if (friendsSecureSrv != nullptr) tasksMgr.push(friendsSecureSrv->process());
        if (splatoonAuthSrv != nullptr) tasksMgr.push(splatoonAuthSrv->process());
        if (splatoonSecureSrv != nullptr) tasksMgr.push(splatoonSecureSrv->process());
        tasksMgr.push(util::GlobalTaskScheduler::getInstance().process());
    }

    util::GlobalTaskScheduler::getInstance().stop();
    if (grpcServer != nullptr) grpcServer->stop();
    if (sharedState != nullptr) sharedState->close();
    if (splatoonAuthDB != nullptr) splatoonAuthDB->close();
    if (friendsSecureDB != nullptr) friendsSecureDB->close();
    if (friendsAuthDB != nullptr) friendsAuthDB->close();
    if (accountsDB != nullptr) accountsDB->close();
    if (bossDB != nullptr) bossDB->close();
    if (splatoonSecureSrv != nullptr) splatoonSecureSrv->stop();
    if (splatoonAuthSrv != nullptr) splatoonAuthSrv->stop();
    if (friendsSecureSrv != nullptr) friendsSecureSrv->stop();
    if (friendsAuthSrv != nullptr) friendsAuthSrv->stop();
    if (httpServer != nullptr) httpServer->stop();
    if (managementServer != nullptr) managementServer->stop();
    socketManager->cleanup();
    certManager->cleanup();

    // Cleanup sockets (only needed on Windows)
    sock::cleanup();

    return 0;
}
