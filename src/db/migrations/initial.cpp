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
            sqlCmds.emplace_back("CREATE TABLE friendships (pid INTEGER NOT NULL, friend_pid INTEGER NOT NULL, became_friends TEXT NOT NULL,"
                                 "PRIMARY KEY (pid, friend_pid), FOREIGN KEY(pid) REFERENCES users(pid), FOREIGN KEY(friend_pid) REFERENCES users(pid));");
            sqlCmds.emplace_back("CREATE TABLE user_info (pid INTEGER NOT NULL, show_presence INTEGER NOT NULL DEFAULT (1), "
                                 "show_playing INTEGER NOT NULL DEFAULT (1), block_requests INTEGER NOT NULL DEFAULT (0), "
                                 "nna_info BLOB NOT NULL, presence BLOB NOT NULL, comment BLOB NOT NULL, PRIMARY KEY (pid), "
                                 "last_online TEXT NOT NULL, FOREIGN KEY(pid) REFERENCES users(pid));");
            sqlCmds.emplace_back("CREATE TABLE game_servers (id TEXT NOT NULL, host TEXT NOT NULL, port INTEGER NOT NULL, "
                                 "PRIMARY KEY (id));");
            sqlCmds.emplace_back("CREATE TABLE game_server_access (pid INTEGER NOT NULL, game_server_id TEXT NOT NULL, "
                                 "password TEXT NOT NULL, PRIMARY KEY (pid, game_server_id), FOREIGN KEY(pid) REFERENCES users(pid),"
                                 "FOREIGN KEY(game_server_id) REFERENCES game_servers(id));");
            break;
    }

    for (auto & sqlCmd : sqlCmds) {
        std::unique_ptr<Command> command = Database::craftVoidCommand(sqlCmd);
        commandIds.emplace_back(db->queueCommand(std::move(command), false), sqlCmd);
    }

    db->processQueue();
    db->waitForQueue(nullptr);

    for (const auto& commandId : commandIds) {
        db->clearCommandMutex(commandId.first);
        std::unique_ptr<Result> result = db->getResult(commandId.first);
        if (result == nullptr) {
            logger->log(Logger::level::FAILURE, Logger::group::DB, "Failed to get result for command: " + commandId.second);
            return false;
        }

        if (result->status != DBResultStatus::SUCCESS) {
            logger->log(Logger::level::FAILURE, Logger::group::DB, "Failed to execute command: " + commandId.second);
            return false;
        }
    }

    return true;
}

}