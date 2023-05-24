#include "argParser.hpp"
#include "settingsManager.hpp"
#include "ssl/certManager.hpp"
#include "http/server.hpp"

#include "db/sqlite3Database.hpp"

int main(int argc, char** argv) {
    // Init OpenSSL
    OpenSSL_add_all_algorithms();

    std::shared_ptr<Logger::Logger> logger(new Logger::Logger());

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, logger)) return 1;

    logger->setMinLevel(serverOptions.minLogLevel);

    std::shared_ptr<SettingsManager> settingsMgr(new SettingsManager(logger));
    if (!settingsMgr->init(serverOptions)) return 1;

    CertManager certManager = CertManager(settingsMgr, logger);
    if (settingsMgr->isAccountEnabled() || settingsMgr->isBOSSEnabled()) {
        if (!certManager.init()) {
            certManager.cleanup();
            return 1;
        }

//        HTTP_Server httpServer = HTTP_Server(&logger, &settingsMgr, &certManager);
//        httpServer.listen();

        db::Database* db;
        db = new db::sqlite3Database(logger, "data/test.db");
        db->init();

        auto* sqlite3DB = dynamic_cast<db::sqlite3Database*>(db);
        sqlite3DB->run();

        const std::string command = "CREATE TABLE IF NOT EXISTS test2 (id INTEGER PRIMARY KEY, name TEXT NOT NULL);";
        auto* dbCommand = new db::Command(db::commandType::GENERIC,
                                          {std::any(command), std::any(std::vector<dbDataType>{}),
                                         std::any(std::vector<DBData*>{}), std::any(std::vector<dbDataType>{})});
        bool shouldEnd = false;
        sqlite3DB->queueCommand(dbCommand, false);
        sqlite3DB->processQueue();
        sqlite3DB->waitForQueue(&shouldEnd);
        logger->log(Logger::level::INFO, Logger::group::SETUP, "Queue finished");
        sqlite3DB->close();
    }

    return 0;
}
