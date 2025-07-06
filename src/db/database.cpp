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
    DBGenericCommand cmd {
        .cmd = command,
        .bindTypes = {},
        .bindData = {},
        .resultTypes = {}
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GENERIC, std::any(cmd));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserByPIDCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_USER_BY_PID,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserByUsernameCommand(const std::string& username) {
    DBUsernameQuery query {
        .username = username
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_USER_BY_USERNAME,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetGameServerAccessCommand(uint32_t pid) {
    DBGameServerAccessQuery query {
        .pid = pid,
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_GAME_SERVER_ACCESS,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertGameServerAccessCommand(uint32_t pid, const std::string& password) {
    DBGameServerAccessData data {
        .pid = pid,
        .password = password
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::INSERT_GAME_SERVER_ACCESS,
                                               std::any(data));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserInfoCommand(uint32_t pid) {
    DBPidQuery query {
            .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_USER_INFO,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertUserInfoCommand(uint32_t pid, bool showOnline, bool showPlaying,
                                                              bool blockRequests, const std::vector<uint8_t>& nnaInfo,
                                                              const std::vector<uint8_t>& presence,
                                                              const std::vector<uint8_t>& comment,
                                                              datetime_t lastOnline) {
    DBUserInfoData data {
        .pid = pid,
        .showPresence = showOnline,
        .showPlaying = showPlaying,
        .blockRequests = blockRequests,
        .nnaInfo = nnaInfo,
        .presence = presence,
        .comment = comment,
        .lastOnline = lastOnline
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::INSERT_USER_INFO,
                                               std::any(data));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftUpdateUserInfoCommand(uint32_t pid, std::optional<bool> showOnline,
                                                              std::optional<bool> showPlaying,
                                                              std::optional<bool> blockRequests,
                                                              std::optional<std::vector<uint8_t>> nnaInfo,
                                                              std::optional<std::vector<uint8_t>> presence,
                                                              std::optional<std::vector<uint8_t>> comment,
                                                              std::optional<datetime_t> lastOnline) {
    DBUserInfoUpdate update {
        .pid = pid,
        .showPresence = showOnline,
        .showPlaying = showPlaying,
        .blockRequests = blockRequests,
        .nnaInfo = std::move(nnaInfo),
        .presence = std::move(presence),
        .comment = std::move(comment),
        .lastOnline = lastOnline
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::UPDATE_USER_INFO,
                                               std::any(update));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetFriendsInfoCommand(uint32_t pid) {
    DBPidQuery query {
            .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_FRIENDS_INFO,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserProfileCommand(uint32_t pid) {
    DBPidQuery query {
            .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_USER_PROFILE,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetDeviceAttributesCommand(uint32_t pid, uint32_t deviceId) {
    DBDeviceAttributesQuery query {
            .pid = pid,
            .deviceId = deviceId
    };

    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GET_DEVICE_ATTRIBUTES,
                                               std::any(query));

    return dbCommand;
}

std::shared_ptr<Database> Database::createDatabase(const json& config, const std::shared_ptr<Logger::Logger>& logger) {
    if (config["type"].get<std::string>() == "SQLite3") {
        auto* db = new sqlite3Database(std::move(logger), config["path"].get<std::string>());
        return std::shared_ptr<Database>((Database*) db);
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::DB, "Database type " +
                                                               config["type"].get<std::string>() + " is not supported.");
        return nullptr;
    }
}

std::shared_ptr<Promise> Database::runCommand(std::unique_ptr<Command> command,
                                              std::shared_ptr<std::mutex> promisesMutex,
                                              std::shared_ptr<std::condition_variable> promisesCV,
                                              std::shared_ptr<std::queue<std::shared_ptr<Promise>>> promisesQueue) {
    if (shouldStop) return std::make_shared<Promise>();

    auto promise = queueCommand(std::move(command), std::move(promisesMutex), std::move(promisesCV), std::move(promisesQueue));

    processQueue();

    return promise;
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
            if (command->data.type() != typeid(DBGenericCommand)) {
                return false;
            }

            break;

        case DBCommandType::GET_USER_BY_PID:
        case DBCommandType::GET_USER_INFO:
        case DBCommandType::GET_FRIENDS_INFO:
        case DBCommandType::GET_USER_PROFILE:
            if (command->data.type() != typeid(DBPidQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_USER_BY_USERNAME:
            // Get user by username commands need the username of the user to get.
            if (command->data.type() != typeid(DBUsernameQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_GAME_SERVER_ACCESS:
            // Get game server access commands need the PID of the user to get and the ID of the game server.
            if (command->data.type() != typeid(DBGameServerAccessQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_GAME_SERVER_ACCESS:
            // Insert game server access commands need the PID of the user to get and the ID of the game server.
            if (command->data.type() != typeid(DBGameServerAccessData)) {
                return false;
            }

            break;

        case DBCommandType::UPDATE_USER_INFO:
            // Update the user information. Not all fields need to be updated, so they are optionals.
            if (command->data.type() != typeid(DBUserInfoUpdate)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_USER_INFO:
            // Insert user information commands need the PID of the user to get and the user information to insert.
            if (command->data.type() != typeid(DBUserInfoData)) {
                return false;
            }

            break;

        case DBCommandType::GET_DEVICE_ATTRIBUTES:
            // Get device attributes commands need the PID of the user to get (to get the attributes of the linked account).
            if (command->data.type() != typeid(DBDeviceAttributesQuery)) {
                return false;
            }

            break;
    }

    return true;
}

} // namespace db