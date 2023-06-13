#include <memory>

#include "argParser.hpp"
#include "settingsManager.hpp"
#include "db/database.hpp"
#include "db/migrations/migrations.hpp"
#include "ssl/certManager.hpp"
#include "socket/socket.hpp"
#include "socket/socketManager.hpp"
#include "socket/tlsSocket.hpp"

void acceptCallbackTest(unsigned int socketId, unsigned int newSocketId, sock::IPv4Dir addr) {
    std::cout << "New connection from " << (int) addr.a << "." << (int) addr.b << "."
              << (int) addr.c << "." << (int) addr.d << ":" << addr.port << std::endl;
}

void closeCallbackTest(unsigned int socketId) {
    std::cout << "Socket " << socketId << " closed." << std::endl;
}

void readCallbackTest(unsigned int socketId, std::vector<unsigned char> data) {
    std::cout << "Socket " << socketId << " read: " << std::string((char*) data.data()) << std::endl;
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
        db::migrations::migrate(logger, db, db::DBType::SQLITE3, db->getVersion());
    }

    std::shared_ptr<SocketManager> socketManager(new SocketManager(logger));

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

        std::shared_ptr<sock::TLSSocket> socket = std::make_shared<sock::TLSSocket>(true, certManager->getSSLKey(), certManager->getSSLCert());
        socket->setBlocking(false);
        int opt = 1;
        socket->setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(8080);
        socket->bind((struct sockaddr*)&address, sizeof(address));
        socket->listen();

        socketManager->addTCPSocket(socket, acceptCallbackTest, closeCallbackTest,
                                    readCallbackTest, closeCallbackTest);

        certManager->cleanup();
    }

    while (true) {
        socketManager->process();
    }

    db->close();

    // Cleanup sockets (only needed on Windows)
    sock::cleanup();

    return 0;
}
