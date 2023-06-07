#include <memory>

#include "argParser.hpp"
#include "settingsManager.hpp"
#include "db/database.hpp"
#include "db/migrations/migrations.hpp"
#include "ssl/certManager.hpp"

#include "http/server.hpp"

int main(int argc, char** argv) {
    std::shared_ptr<Logger::Logger> logger(new Logger::Logger());

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, logger)) return 1;

    logger->setMinLevel(serverOptions.minLogLevel);

    std::shared_ptr<SettingsManager> settingsMgr(new SettingsManager(logger));
    if (!settingsMgr->init(serverOptions)) return 1;

    std::shared_ptr<db::Database> db = db::Database::createDatabase(settingsMgr->getDBSettings(), logger);
    db->init();
    db->run();
    if (db->getVersion() != db::CURRENT_VERSION) {
        db::migrations::migrate(logger, db, db::DBType::SQLITE3, db->getVersion());
    }

    std::shared_ptr<CertManager> certManager(new CertManager(settingsMgr, logger));
    if (settingsMgr->isAccountEnabled() || settingsMgr->isBOSSEnabled()) {
        if (!certManager->init()) {
            certManager->cleanup();
            db->close();
            return 1;
        }

//        HTTP_Server httpServer = HTTP_Server(&logger, &settingsMgr, &certManager);
//        httpServer.listen();

        certManager->cleanup();
    }

    db->close();

    return 0;
}
