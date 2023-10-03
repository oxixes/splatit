#include "database.hpp"
#include "sqlite3Database.hpp"

#include <utility>

namespace db {

Database::Database(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version) {
    this->logger = std::move(logger);
    this->dbType = type;
    this->dbVersion = version;
}

std::unique_ptr<Command> Database::craftVoidCommand(const std::string& command) {
    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GENERIC,
        std::vector<std::any>{std::any(command), std::any(std::vector<dbDataType>{}),
        std::any(std::vector<std::shared_ptr<DBData>>{}), std::any(std::vector<dbDataType>{})});

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserByPIDCommand(uint32_t pid) {
    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_USER_BY_PID,
        std::vector<std::any>{std::any(pid)});

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserByUsernameCommand(const std::string& username) {
    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_USER_BY_USERNAME,
        std::vector<std::any>{std::any(username)});

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetGameServerAccessCommand(uint32_t pid, const std::string& serverId) {
    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_GAME_SERVER_ACCESS,
        std::vector<std::any>{std::any(pid), std::any(serverId)});

    return dbCommand;
}

std::unique_ptr<Result> Database::getResult(uint32_t commandID) {
    std::unique_lock<std::mutex> lock(resultsMutex);

    auto result = std::find_if(this->results.begin(), this->results.end(),
        [commandID](const std::unique_ptr<Result>& result) {
            return result->commandId == commandID;
        });

    if (result == this->results.end()) return nullptr;

    auto resultPtr = std::move(*result);
    results.erase(result);

    return resultPtr;
}

std::shared_ptr<Database> Database::createDatabase(const json& config, std::shared_ptr<Logger::Logger> logger) {
    if (config["type"].get<std::string>() == "SQLite3") {
        auto* db = new sqlite3Database(std::move(logger), config["path"].get<std::string>());
        return std::shared_ptr<Database>((Database*) db);
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::DB, "Database type " +
                                                               config["type"].get<std::string>() + " is not supported.");
        return nullptr;
    }
}

uint32_t Database::runCommand(std::shared_ptr<Database> db, std::unique_ptr<Command> command,
                const std::function<uint32_t(std::function<void()>)>& registerCloseCall,
                const std::function<void(uint32_t)>& unregisterCloseCall,
                bool& shouldStop) {

    uint32_t cmdId = db->queueCommand(std::move(command), true);
    unsigned int closeCallId;
    if (registerCloseCall != nullptr)
        closeCallId = registerCloseCall([cmdId, &db]() { db->notifyCommand(cmdId); });
    // The server may be set to stop while the closeCall is being registered, which may cause it not to be called,
    // so we check if it should stop here.
    if (!shouldStop) {
        db->processQueue();
        db->waitForCommand(cmdId, std::make_shared<bool>(shouldStop));
    }

    if (shouldStop) throw std::runtime_error("Server is stopping");
    db->clearCommandMutex(cmdId);
    if (unregisterCloseCall != nullptr && registerCloseCall != nullptr)
        unregisterCloseCall(closeCallId);

    return cmdId;
}

DBType Database::getType() const {
    return this->dbType;
}

DBVersion Database::getVersion() const {
    return this->dbVersion;
}

bool Database::verifyCommandArgs(const std::unique_ptr<Command>& command) {
    switch (command->type) {
        case DBCommandType::GENERIC:
            // Generic commands can run any SQL command, therefore we need the SQL string,
            // the types of the data to bind, the data to bind, and the types of the data to return.
            // (Actually the same would be necessary for non-SQL commands, but we don't have any of those yet.)
            if (command->data.size() != 4 || command->data[0].type() != typeid(std::string) ||
                command->data[1].type() != typeid(std::vector<dbDataType>) ||
                command->data[2].type() != typeid(std::vector<std::shared_ptr<DBData>>) ||
                command->data[3].type() != typeid(std::vector<dbDataType>)) {
                return false;
            }

            break;

        case DBCommandType::GET_USER_BY_PID:
            // Get user by PID commands need the PID of the user to get.
            if (command->data.size() != 1 || command->data[0].type() != typeid(uint32_t)) {
                return false;
            }

            break;

        case DBCommandType::GET_USER_BY_USERNAME:
            // Get user by username commands need the username of the user to get.
            if (command->data.size() != 1 || command->data[0].type() != typeid(std::string)) {
                return false;
            }

            break;

        case DBCommandType::GET_GAME_SERVER_ACCESS:
            // Get game server access commands need the PID of the user to get and the ID of the game server.
            if (command->data.size() != 2 || command->data[0].type() != typeid(uint32_t) ||
                command->data[1].type() != typeid(std::string)) {
                return false;
            }

            break;

        case DBCommandType::GET_USER_INFO:
            // Get the user information.
            if (command->data.size() != 1 || command->data[0].type() != typeid(uint32_t)) {
                return false;
            }

            break;
    }

    return true;
}

} // namespace db