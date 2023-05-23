#include "argParser.hpp"
#include "settingsManager.hpp"
#include "ssl/certManager.hpp"
#include "http/server.hpp"

#include "db/sqlite3Database.hpp"


#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    // Init OpenSSL
    OpenSSL_add_all_algorithms();

    Logger::Logger logger;

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, &logger)) return 1;

    logger.setMinLevel(serverOptions.minLogLevel);

    SettingsManager settingsMgr = SettingsManager(&logger);
    if (!settingsMgr.init(serverOptions)) return 1;

    CertManager certManager = CertManager(&settingsMgr, &logger);
    if (settingsMgr.isAccountEnabled() || settingsMgr.isBOSSEnabled()) {
        if (!certManager.init()) {
            certManager.cleanup();
            return 1;
        }

//        HTTP_Server httpServer = HTTP_Server(&logger, &settingsMgr, &certManager);
//        httpServer.listen();

        Database* db;
        db = new sqlite3Database(&logger, "data/test.db");
        db->init();

        auto* sqlite3DB = dynamic_cast<sqlite3Database*>(db);
//        const std::string command = "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, name TEXT NOT NULL);";
//        sqlite3_stmt* statement;
//        sqlite3DB->craftCommand(command, &statement);
//        sqlite3DB->runStatement(statement, {}, nullptr);

        sqlite3DB->run();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        sqlite3DB->close();
    }

    return 0;
}
