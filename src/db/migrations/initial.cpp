#include "migrations.hpp"

namespace db::migrations {

bool migration_initial(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    std::vector<std::pair<int, std::string>> commandIds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE users (pid INTEGER, username TEXT NOT NULL, password TEXT NOT NULL, PRIMARY KEY (pid));");
            sqlCmds.emplace_back("CREATE TABLE friendships (pid INTEGER NOT NULL, friend_pid INTEGER NOT NULL, PRIMARY KEY (pid, friend_pid));");
            sqlCmds.emplace_back("CREATE TABLE user_info (pid INTEGER NOT NULL, show_presence INTEGER NOT NULL DEFAULT (1), "
                                 "show_playing INTEGER NOT NULL DEFAULT (1), block_requests INTEGER NOT NULL DEFAULT (0), "
                                 "mii BLOB, PRIMARY KEY (pid));");
            break;
    }

    for (auto sql = sqlCmds.begin(); sql != sqlCmds.end() - 1; ++sql) {
        std::unique_ptr<Command> command = Database::craftVoidCommand(*sql);
        commandIds.emplace_back(db->queueCommand(std::move(command), false), *sql);
    }
    std::unique_ptr<Command> command = Database::craftVoidCommand(sqlCmds.back());
    commandIds.emplace_back(db->queueCommand(std::move(command), true), sqlCmds.back());

    db->processQueue();
    db->waitForCommand(commandIds.back().first, nullptr);

    for (const auto& commandId : commandIds) {
        db->clearCommandMutex(commandId.first);
        std::unique_ptr<Result> result = db->getResult(commandId.first);
        if (result == nullptr) {
            logger->log(Logger::level::ERROR, Logger::group::DB, "Failed to get result for command: " + commandId.second);
            return false;
        }

        if (result->status != DBResultStatus::SUCCESS) {
            logger->log(Logger::level::ERROR, Logger::group::DB, "Failed to execute command: " + commandId.second);
            return false;
        }
    }

    return true;
}

}