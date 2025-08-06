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

    auto dbCommand = std::make_unique<Command>(DBCommandType::GENERIC, std::any(cmd));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserByPIDCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_BY_PID,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserByUsernameCommand(const std::string& username) {
    DBUsernameQuery query {
        .username = username
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_BY_USERNAME,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetGameServerAccessCommand(uint32_t pid) {
    DBGameServerAccessQuery query {
        .pid = pid,
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_GAME_SERVER_ACCESS,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertGameServerAccessCommand(uint32_t pid, const std::string& password) {
    DBGameServerAccessData data {
        .pid = pid,
        .password = password
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_GAME_SERVER_ACCESS,
                                               std::any(data));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserInfoCommand(uint32_t pid) {
    DBPidQuery query {
            .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_INFO,
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

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_USER_INFO,
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

    auto dbCommand = std::make_unique<Command>(DBCommandType::UPDATE_USER_INFO,
                                               std::any(update));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetFriendsInfoCommand(uint32_t pid) {
    DBPidQuery query {
            .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_FRIENDS_INFO,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserProfileCommand(uint32_t pid) {
    DBPidQuery query {
            .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_PROFILE,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetDeviceAttributesCommand(uint32_t pid, uint32_t deviceId) {
    DBDeviceAttributesQuery query {
            .pid = pid,
            .deviceId = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_DEVICE_ATTRIBUTES,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetAgreementCommand(const std::string& type, const std::string& country,
                                                            const std::string& language, const std::optional<int> version) {
    DBGetAgreementQuery query {
        .type = type,
        .country = country,
        .language = language,
        .version = version
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_AGREEMENT,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetDeviceCommand(uint32_t deviceId) {
    DBIdQuery query {
        .id = static_cast<int64_t>(deviceId)
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_DEVICE, std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetLatestPidCommand() {
    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_LATEST_PID, std::any());
    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetOwnershipCommand(uint32_t pid, uint32_t deviceId) {
    DBOwnershipQuery query {
        .pid = pid,
        .deviceId = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_OWNERSHIP,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetLatestOwnershipCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_LATEST_OWNERSHIP,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftHasActiveOwnershipCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::HAS_ACTIVE_OWNERSHIP,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateDeviceCommand(uint32_t deviceId, const std::string& language,
                                                                    uint32_t platformId, uint32_t region,
                                                                    const std::string& serialNumber,
                                                                    const std::string& systemVersion,
                                                                    const std::string& type,
                                                                    const std::string& updatedBy,
                                                                    datetime_t lastUpdated) {
    DBDeviceInsertOrUpdateQuery query {
        .deviceId = deviceId,
        .language = language,
        .platformId = platformId,
        .region = region,
        .serialNumber = serialNumber,
        .systemVersion = systemVersion,
        .type = type,
        .updatedBy = updatedBy,
        .lastUpdated = lastUpdated
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_DEVICE,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateUserAgreementCommand(uint32_t pid, const std::string& type,
                                                                           int version, const std::string& country,
                                                                           datetime_t signedAt) {
    DBUserAgreementInsertOrUpdateQuery query {
        .pid = pid,
        .type = type,
        .version = version,
        .country = country,
        .signedAt = signedAt
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_USER_AGREEMENT,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateMiiCommand(std::optional<int64_t> miiId,
                                                                 const std::string& hash, const std::string& name,
                                                                 bool primary, const std::string& data) {
    DBMiiInsertOrUpdateQuery query {
        .miiId = miiId,
        .hash = hash,
        .name = name,
        .primary = primary,
        .data = data
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_MII,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateEmailCommand(std::optional<int64_t> emailId,
                                                                   const std::string& email, bool parent,
                                                                   bool primary, bool reachable,
                                                                   const std::string& type,
                                                                   const std::string& updatedBy,
                                                                   bool validated,
                                                                   datetime_t validatedAt) {
    DBEmailInsertOrUpdateQuery query {
        .emailId = emailId,
        .email = email,
        .parent = parent,
        .primary = primary,
        .reachable = reachable,
        .type = type,
        .updatedBy = updatedBy,
        .validated = validated,
        .validatedAt = validatedAt
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_EMAIL,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertProfileCommand(uint32_t pid, const std::string& username,
                                                             const std::string& password, int64_t emailId,
                                                             int64_t miiId, bool gender, int64_t region,
                                                             const std::string& tz, const std::string& language,
                                                             bool active, bool marketing, bool offDevice,
                                                             const std::string& birthdate, const std::string& country,
                                                             datetime_t created, datetime_t updated) {
    DBUserProfileInsertQuery query {
        .pid = pid,
        .username = username,
        .password = password,
        .emailId = emailId,
        .miiId = miiId,
        .gender = gender,
        .region = region,
        .tz = tz,
        .language = language,
        .active = active,
        .marketing = marketing,
        .offDevice = offDevice,
        .birthdate = birthdate,
        .country = country,
        .created = created,
        .updated = updated
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_USER_PROFILE,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateDeviceAttributesCommand(uint32_t deviceId, uint32_t pid,
                                                                              const std::string& name,
                                                                              const std::string& value,
                                                                              datetime_t createdDate) {
    DBDeviceAttributesInsertOrUpdateQuery query {
        .deviceId = deviceId,
        .pid = pid,
        .name = name,
        .value = value,
        .createdDate = createdDate
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_DEVICE_ATTRIBUTES,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateOwnershipCommand(uint32_t pid, uint32_t deviceId,
                                                                       const std::string& status,
                                                                       datetime_t lastUpdated) {
    DBOwnershipInsertOrUpdateQuery query {
        .pid = pid,
        .deviceId = deviceId,
        .status = status,
        .lastUpdated = lastUpdated
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_OWNERSHIP,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftUpdateUserProfileCommand(uint32_t pid, std::optional<std::string> username,
                                                                 std::optional<std::string> password,
                                                                 std::optional<int64_t> emailId,
                                                                 std::optional<int64_t> miiId,
                                                                 std::optional<bool> gender,
                                                                 std::optional<int64_t> region,
                                                                 std::optional<std::string> tz,
                                                                 std::optional<std::string> language,
                                                                 std::optional<bool> active,
                                                                 std::optional<bool> marketing,
                                                                 std::optional<bool> offDevice,
                                                                 std::optional<std::string> birthdate,
                                                                 std::optional<std::string> country,
                                                                 std::optional<datetime_t> created,
                                                                 std::optional<datetime_t> updated) {
    DBUserProfileUpdateQuery query {
        .pid = pid,
        .username = std::move(username),
        .password = std::move(password),
        .emailId = emailId,
        .miiId = miiId,
        .gender = gender,
        .region = region,
        .tz = std::move(tz),
        .language = std::move(language),
        .active = active,
        .marketing = marketing,
        .offDevice = offDevice,
        .birthdate = std::move(birthdate),
        .country = std::move(country),
        .created = created,
        .updated = updated
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::UPDATE_USER_PROFILE,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteMiiCommand(int64_t miiId) {
    DBIdQuery query {
        .id = miiId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_MII, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteEmailCommand(int64_t emailId) {
    DBIdQuery query {
        .id = emailId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_EMAIL, std::any(query));
    return dbCommand;
}


std::shared_ptr<Database> Database::createDatabase(const json& config, const std::shared_ptr<Logger::Logger>& logger) {
    if (config["type"].get<std::string>() == "SQLite3") {
        auto* db = new sqlite3Database(logger, config["path"].get<std::string>());
        return std::shared_ptr<Database>(static_cast<Database *>(db));
    }

    logger->log(Logger::level::FAILURE, Logger::group::DB, "Database type " +
                                                           config["type"].get<std::string>() + " is not supported.");
    return nullptr;
}

async::ManualTask<Result> Database::runCommand(std::unique_ptr<Command> command) {
    if (*shouldStop) return async::ManualTask<Result>();

    auto task = queueCommand(std::move(command));

    processQueue();

    return task;
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

        case DBCommandType::GET_USER_BY_PID: // Gets the user (pid, username and password) by PID.
        case DBCommandType::GET_USER_INFO: // Gets the user friend information by PID.
        case DBCommandType::GET_FRIENDS_INFO: // Gets the friends information of a user by PID.
        case DBCommandType::GET_USER_PROFILE: // Gets the user profile information by PID.
        case DBCommandType::GET_LATEST_OWNERSHIP: // Gets the latest ownership of a device by PID.
        case DBCommandType::HAS_ACTIVE_OWNERSHIP: // Checks if a user has an active ownership of a device by PID.
            if (command->data.type() != typeid(DBPidQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_MII: // Deletes a Mii by its ID.
        case DBCommandType::DELETE_EMAIL: // Deletes an email by its ID.
        case DBCommandType::GET_DEVICE: // Gets a device by its ID.
            if (command->data.type() != typeid(DBIdQuery)) {
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

        case DBCommandType::GET_AGREEMENT:
            // Get agreement commands need the type, country, language, and optionally the version of the agreement (latest if not specified).
            if (command->data.type() != typeid(DBGetAgreementQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_LATEST_PID:
            // Gets the latest PID in order to assign a new PID to a user.
            // Get latest PID command does not need any data.
            if (command->data.has_value()) {
                return false;
            }

            break;

        case DBCommandType::GET_OWNERSHIP:
            // Gets the ownership of a device by PID and device ID.
            if (command->data.type() != typeid(DBOwnershipQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_DEVICE:
            // Inserts or updates a device in the database.
            if (command->data.type() != typeid(DBDeviceInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_USER_AGREEMENT:
            // Inserts or updates a user agreement in the database.
            if (command->data.type() != typeid(DBUserAgreementInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_MII:
            // Inserts or updates a Mii in the database.
            if (command->data.type() != typeid(DBMiiInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_EMAIL:
            // Inserts or updates an email in the database.
            if (command->data.type() != typeid(DBEmailInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_USER_PROFILE:
            // Inserts a user profile in the database.
            if (command->data.type() != typeid(DBUserProfileInsertQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_DEVICE_ATTRIBUTES:
            // Inserts or updates device attributes in the database.
            if (command->data.type() != typeid(DBDeviceAttributesInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_OWNERSHIP:
            // Inserts or updates device ownership in the database.
            if (command->data.type() != typeid(DBOwnershipInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::UPDATE_USER_PROFILE:
            // Updates a user profile in the database.
            if (command->data.type() != typeid(DBUserProfileUpdateQuery)) {
                return false;
            }

            break;
    }

    return true;
}

} // namespace db