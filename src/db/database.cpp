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

std::unique_ptr<Command> Database::craftGetUserInfoByPidCommand(uint32_t pid) {
    DBPidQuery query {
            .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_INFO_BY_PID,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserInfoByUsernameCommand(const std::string &username) {
    DBUsernameQuery query {
        .username = username
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_INFO_BY_USERNAME,
                                               std::any(query));
    return dbCommand;
}


std::unique_ptr<Command> Database::craftInsertUserInfoCommand(uint32_t pid, const std::string& username,
                                                              bool showOnline, bool showPlaying,
                                                              bool blockRequests, const std::vector<uint8_t>& nnaInfo,
                                                              const std::vector<uint8_t>& presence,
                                                              const std::vector<uint8_t>& comment,
                                                              datetime_t lastOnline) {
    DBUserInfoData data {
        .pid = pid,
        .username = username,
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

std::unique_ptr<Command> Database::craftAddFriendCommand(uint32_t pid, uint32_t friendPid, datetime_t becameFriends) {
    DBFriendshipInsertQuery query {
        .pid = pid,
        .friendPid = friendPid,
        .becameFriends = becameFriends
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::ADD_FRIEND,
                                               std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftBlockFriendCommand(uint32_t pid, uint32_t blockedPid, datetime_t createdAt,
                                                           const std::vector<uint8_t> &gameKey) {
    DBBlockInsertQuery query {
        .pid = pid,
        .blockedPid = blockedPid,
        .createdAt = createdAt,
        .gameKey = gameKey
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::BLOCK_FRIEND,
                                               std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftUpdateUserInfoCommand(uint32_t pid, std::optional<std::string> username,
                                                              std::optional<bool> showOnline,
                                                              std::optional<bool> showPlaying,
                                                              std::optional<bool> blockRequests,
                                                              std::optional<std::vector<uint8_t>> nnaInfo,
                                                              std::optional<std::vector<uint8_t>> presence,
                                                              std::optional<std::vector<uint8_t>> comment,
                                                              std::optional<datetime_t> lastOnline) {
    DBUserInfoUpdate update {
        .pid = pid,
        .username = std::move(username),
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

std::unique_ptr<Command> Database::craftGetFriendRequestCommand(int64_t id) {
    DBIdQuery query {
        .id = id
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_FRIEND_REQUEST,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetSentFriendRequestsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_SENT_FRIEND_REQUESTS,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetReceivedFriendRequestsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_RECEIVED_FRIEND_REQUESTS,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetBlockedFriendsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_BLOCKED_FRIENDS,
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

std::unique_ptr<Command> Database::craftGetUserMiiCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_MII,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetUserEmailCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_USER_EMAIL,
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

std::unique_ptr<Command> Database::craftGetAllAgreementsCommand(std::optional<std::string> type,
                                                            std::optional<std::string> country,
                                                            std::optional<std::string> language,
                                                            std::optional<int> version,
                                                            std::vector<std::pair<std::string, bool>> sortBy,
                                                            uint64_t pageSize, uint64_t pageNumber) {
    DBAgreementsQuery query {
        .type = std::move(type),
        .country = std::move(country),
        .language = std::move(language),
        .version = version,
        .sortBy = std::move(sortBy),
        .pageSize = pageSize,
        .pageNumber = pageNumber
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_ALL_AGREEMENTS,
                                               std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetAgreementCommand(const std::string& type, const std::string& country,
                                                            const std::string& language, const std::optional<int> version) {
    DBAgreementQuery query {
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

std::unique_ptr<Command> Database::craftGetOwnershipCommand(uint32_t pid, uint32_t deviceId) {
    DBOwnershipQuery query {
        .pid = pid,
        .deviceId = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_OWNERSHIP,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetPersistentNotificationsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_PERSISTENT_NOTIFICATIONS,
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

std::unique_ptr<Command> Database::craftGetOwnershipsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_OWNERSHIPS,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInactivateDeviceOwnershipsCommand(uint32_t deviceId) {
    DBIdQuery query {
        .id = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INACTIVATE_DEVICE_OWNERSHIPS,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdatePersistentNotificationCommand(std::optional<int64_t>id, uint32_t forPid,
                                                                            int64_t value1, uint32_t value2, uint32_t value3,
                                                                            uint32_t value4, const std::string &text) {
    DBPersistentNotificationInsertOrUpdateQuery query {
        .id = id,
        .forPid = forPid,
        .value1 = value1,
        .value2 = value2,
        .value3 = value3,
        .value4 = value4,
        .text = text
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_PERSISTENT_NOTIFICATION,
                                               std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateDeviceCommand(uint32_t deviceId, const std::string& language,
                                                                    uint32_t platformId, uint32_t region,
                                                                    const std::string& serialNumber,
                                                                    const std::string& systemVersion,
                                                                    const std::string& type,
                                                                    const std::string& updatedBy,
                                                                    const std::string& status,
                                                                    bool banned,
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
        .status = status,
        .banned = banned,
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

std::unique_ptr<Command> Database::craftInsertOrUpdateAgreementCommand(const std::string& type, int version,
                                                                       const std::string& country,
                                                                       const std::string& language,
                                                                       const std::string& languageName,
                                                                       datetime_t publishedAt,
                                                                       const std::string& mainTitle,
                                                                       const std::string& subTitle,
                                                                       const std::string& agreeText,
                                                                       const std::string& disagreeText,
                                                                       const std::string& mainText,
                                                                       const std::string& subText) {
    DBAgreementData query {
        .type = type,
        .version = version,
        .country = country,
        .language = language,
        .languageName = languageName,
        .publishedAt = publishedAt,
        .mainTitle = mainTitle,
        .subTitle = subTitle,
        .agreeText = agreeText,
        .disagreeText = disagreeText,
        .mainText = mainText,
        .subText = subText
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_AGREEMENT,
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
                                                                   datetime_t validatedAt,
                                                                   const std::string& validationCode) {
    DBEmailInsertOrUpdateQuery query {
        .emailId = emailId,
        .email = email,
        .parent = parent,
        .primary = primary,
        .reachable = reachable,
        .type = type,
        .updatedBy = updatedBy,
        .validated = validated,
        .validatedAt = validatedAt,
        .validationCode = validationCode
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_EMAIL,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertProfileCommand(const std::string& username,
                                                             const std::string& password, int64_t emailId,
                                                             int64_t miiId, bool gender, int64_t region,
                                                             const std::string& tz, const std::string& language,
                                                             bool active, bool marketing, bool offDevice,
                                                             const std::string& birthdate, const std::string& country,
                                                             datetime_t created, datetime_t updated) {
    DBUserProfileInsertQuery query {
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

std::unique_ptr<Command> Database::craftInsertOrUpdateFriendRequestCommand(std::optional<int64_t> id, uint32_t fromPid, uint32_t toPid,
                                                                           datetime_t expiresAt, datetime_t createdAt,
                                                                           const std::vector<uint8_t> &data) {
    DBFriendRequestInsertOrUpdateQuery query {
        .id = id,
        .fromPid = fromPid,
        .toPid = toPid,
        .expiresAt = expiresAt,
        .createdAt = createdAt,
        .data = data
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_FRIEND_REQUEST,
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
                                                                 std::optional<datetime_t> updated,
                                                                 std::optional<bool> isAdmin) {
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
        .updated = updated,
        .isAdmin = isAdmin
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::UPDATE_USER_PROFILE,
                                               std::any(query));

    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateSettingCommand(const std::string& key, const std::string& value) {
    DBInsertOrUpdateSettingQuery query {
        .key = key,
        .value = value
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_SETTING, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertOrUpdateFileCommand(const std::string& hash, const std::vector<uint8_t>& data) {
    DBInsertOrUpdateFileQuery query {
        .hash = hash,
        .data = data
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_OR_UPDATE_FILE, std::any(query));
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

std::unique_ptr<Command> Database::craftDeleteUserCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_USER, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteUserOwnershipsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_USER_OWNERSHIPS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteUserAgreementsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_USER_AGREEMENTS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteUserDeviceAttributesCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_USER_DEVICE_ATTRIBUTES, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteAgreementCommand(const std::string& type, const std::string& country,
                                                               const std::string& language, std::optional<int> version) {
    DBAgreementQuery query {
        .type = type,
        .country = country,
        .language = language,
        .version = version
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_AGREEMENT, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteFriendCommand(uint32_t pid, uint32_t friendPid) {
    DBFriendDeleteQuery query {
        .pid = pid,
        .friendPid = friendPid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_FRIEND, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteFriendRequestCommand(int64_t id) {
    DBIdQuery query {
        .id = id
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_FRIEND_REQUEST, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeletePersistentNotificationCommand(int64_t id) {
    DBIdQuery query {
        .id = id
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_PERSISTENT_NOTIFICATION, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteGameServerAccessCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_GAME_SERVER_ACCESS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftUnblockFriendCommand(uint32_t pid, uint32_t blockedPid) {
    DBFriendDeleteQuery query {
        .pid = pid,
        .friendPid = blockedPid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::UNBLOCK_FRIEND, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetSignedAgreementsCommand(uint32_t pid) {
    DBUserAgreementsQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_SIGNED_AGREEMENTS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetSettingCommand(const std::string& key) {
    DBGetSettingQuery query {
        .key = key
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_SETTING, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetFileCommand(const std::string& hash) {
    DBGetFileQuery query {
        .hash = hash
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_FILE, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftListDevicesCommand(std::optional<uint32_t> platform,
                                                           std::optional<uint32_t> region,
                                                           std::optional<bool> banned,
                                                           std::optional<std::string> serialNumber,
                                                           std::optional<std::string> type,
                                                           std::vector<std::pair<std::string, bool>> sortBy,
                                                           uint64_t pageSize,
                                                           uint64_t pageNumber) {
    DBDevicesListQuery query {
        .platform = platform,
        .region = region,
        .banned = banned,
        .serialNumber = std::move(serialNumber),
        .type = std::move(type),
        .sortBy = std::move(sortBy),
        .pageSize = pageSize,
        .pageNumber = pageNumber
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::LIST_DEVICES, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftListAccountsCommand(std::optional<std::string> username,
                                                            std::optional<bool> gender,
                                                            std::optional<uint64_t> region,
                                                            std::optional<bool> active,
                                                            std::vector<std::pair<std::string, bool>> sortBy,
                                                            uint64_t pageSize,
                                                            uint64_t pageNumber) {
    DBAccountsListQuery query {
        .username = std::move(username),
        .gender = gender,
        .region = region,
        .active = active,
        .sortBy = std::move(sortBy),
        .pageSize = pageSize,
        .pageNumber = pageNumber
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::LIST_ACCOUNTS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftCountDevicesCommand(std::optional<uint32_t> platform,
                                                            std::optional<uint32_t> region,
                                                            std::optional<bool> banned,
                                                            std::optional<std::string> serialNumber,
                                                            std::optional<std::string> type) {
    DBDevicesListQuery query {
        .platform = platform,
        .region = region,
        .banned = banned,
        .serialNumber = std::move(serialNumber),
        .type = std::move(type)
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::COUNT_DEVICES, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftCountAccountsCommand(std::optional<std::string> username,
                                                             std::optional<bool> gender,
                                                             std::optional<uint64_t> region,
                                                             std::optional<bool> active) {
    DBAccountsListQuery query {
        .username = std::move(username),
        .gender = gender,
        .region = region,
        .active = active
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::COUNT_ACCOUNTS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteUserDeviceAttributeCommand(uint32_t pid, uint32_t deviceId, const std::string& name) {
    DBDeviceAttributeDeleteQuery query {
        .deviceId = deviceId,
        .pid = pid,
        .name = name
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_USER_DEVICE_ATTRIBUTE, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteDeviceAttributesForOwnershipCommand(uint32_t pid, uint32_t deviceId) {
    DBOwnershipDeleteQuery query {
        .pid = pid,
        .deviceId = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_DEVICE_ATTRIBUTES_FOR_OWNERSHIP, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteAllBlocksByPidCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_ALL_BLOCKS_BY_PID, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteAllFriendRequestsByPidCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_ALL_FRIEND_REQUESTS_BY_PID, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteAllFriendshipsByPidCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_ALL_FRIENDSHIPS_BY_PID, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteAllNotificationsByPidCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_ALL_NOTIFICATIONS_BY_PID, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteUserInfoByPidCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_USER_INFO_BY_PID, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteDeviceCommand(uint32_t deviceId) {
    DBIdQuery query {
        .id = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_DEVICE, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteDeviceOwnershipsCommand(uint32_t deviceId) {
    DBIdQuery query {
        .id = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_DEVICE_OWNERSHIPS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteDeviceAttributesCommand(uint32_t deviceId) {
    DBIdQuery query {
        .id = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_DEVICE_ATTRIBUTES, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteOwnershipCommand(uint32_t pid, uint32_t deviceId) {
    DBOwnershipDeleteQuery query {
        .pid = pid,
        .deviceId = deviceId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_OWNERSHIP, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteUserAgreementCommand(uint32_t pid, const std::string& type,
                                                                   int version, const std::string& country) {
    DBUserAgreementDeleteQuery query {
        .pid = pid,
        .type = type,
        .version = version,
        .country = country
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_USER_AGREEMENT, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftInactivateAllUserOwnershipsCommand(uint32_t pid) {
    DBPidQuery query {
        .pid = pid
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INACTIVATE_ALL_USER_OWNERSHIPS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftCountAgreementsCommand(std::optional<std::string> type,
                                                                std::optional<std::string> country,
                                                                std::optional<std::string> language,
                                                                std::optional<int> version) {
    DBAgreementsQuery query {
        .type = std::move(type),
        .country = std::move(country),
        .language = std::move(language),
        .version = version
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::COUNT_AGREEMENTS, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftInsertTaskCommand(int type, const std::string& params) {
    DBTaskInsertQuery query {
        .type = type,
        .params = params
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::INSERT_TASK, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetAllTasksCommand() {
    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_ALL_TASKS, std::any());
    return dbCommand;
}

std::unique_ptr<Command> Database::craftDeleteTaskCommand(int64_t id) {
    DBIdQuery query {
        .id = id
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::DELETE_TASK, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftUploadFestivalScoreCommand(uint32_t festivalId, uint32_t pid,
                                                                    uint8_t team, uint32_t teamScore) {
    DBFestivalScoreUploadQuery query {
        .festivalId = festivalId,
        .pid = pid,
        .team = team,
        .teamScore = teamScore
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::UPLOAD_FESTIVAL_SCORE, std::any(query));
    return dbCommand;
}

std::unique_ptr<Command> Database::craftGetFestivalTotalsCommand(uint32_t festivalId) {
    DBFestivalIdQuery query {
        .festivalId = festivalId
    };

    auto dbCommand = std::make_unique<Command>(DBCommandType::GET_FESTIVAL_TOTALS, std::any(query));
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
        case DBCommandType::GET_USER_INFO_BY_PID: // Gets the user friend information by PID.
        case DBCommandType::GET_FRIENDS_INFO: // Gets the friends information of a user by PID.
        case DBCommandType::GET_USER_PROFILE: // Gets the user profile information by PID.
        case DBCommandType::GET_USER_MII: // Gets the Mii of a user by PID.
        case DBCommandType::GET_USER_EMAIL: // Gets the email of a user by PID.
        case DBCommandType::GET_LATEST_OWNERSHIP: // Gets the latest ownership of a device by PID.
        case DBCommandType::HAS_ACTIVE_OWNERSHIP: // Checks if a user has an active ownership of a device by PID.
        case DBCommandType::GET_OWNERSHIPS: // Gets the ownerships of a user by PID.
        case DBCommandType::DELETE_USER: // Deletes a user and all of their related data by PID.
        case DBCommandType::DELETE_USER_OWNERSHIPS: // Deletes all ownerships of a user by PID.
        case DBCommandType::DELETE_USER_AGREEMENTS: // Deletes all user agreements by PID.
        case DBCommandType::DELETE_USER_DEVICE_ATTRIBUTES: // Deletes all device attributes of a user by PID.
        case DBCommandType::GET_SENT_FRIEND_REQUESTS: // Gets the sent friend requests of a user by PID.
        case DBCommandType::GET_RECEIVED_FRIEND_REQUESTS: // Gets the received friend requests of a user by PID.
        case DBCommandType::GET_PERSISTENT_NOTIFICATIONS: // Gets the persistent notifications of a user by PID.
        case DBCommandType::GET_BLOCKED_FRIENDS: // Gets the blocked friends of a user by PID.
        case DBCommandType::DELETE_GAME_SERVER_ACCESS: // Deletes the game server access of a user by PID.
        case DBCommandType::DELETE_ALL_BLOCKS_BY_PID:
        case DBCommandType::DELETE_ALL_FRIEND_REQUESTS_BY_PID:
        case DBCommandType::DELETE_ALL_FRIENDSHIPS_BY_PID:
        case DBCommandType::DELETE_ALL_NOTIFICATIONS_BY_PID:
        case DBCommandType::DELETE_USER_INFO_BY_PID:
            if (command->data.type() != typeid(DBPidQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_MII: // Deletes a Mii by its ID.
        case DBCommandType::DELETE_EMAIL: // Deletes an email by its ID.
        case DBCommandType::GET_DEVICE: // Gets a device by its ID.
        case DBCommandType::INACTIVATE_DEVICE_OWNERSHIPS: // Inactivates all ownerships of a device by its ID.
        case DBCommandType::GET_FRIEND_REQUEST: // Gets a friend request by its ID.
        case DBCommandType::DELETE_FRIEND_REQUEST: // Deletes a friend request by its ID.
        case DBCommandType::DELETE_PERSISTENT_NOTIFICATION: // Deletes a persistent notification by its ID.
        case DBCommandType::DELETE_DEVICE_ATTRIBUTES: // Deletes all device attributes of a device by its ID.
        case DBCommandType::DELETE_DEVICE_OWNERSHIPS: // Deletes all ownerships of a device by its ID.
            if (command->data.type() != typeid(DBIdQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_USER_BY_USERNAME: // Gets the user (pid, username and password) by username.
        case DBCommandType::GET_USER_INFO_BY_USERNAME: // Gets the friends server user info by username.
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

        case DBCommandType::GET_SETTING:
            // Get a server setting.
            if (command->data.type() != typeid(DBGetSettingQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_FILE:
            // Get a server file.
            if (command->data.type() != typeid(DBGetFileQuery)) {
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

        case DBCommandType::GET_ALL_AGREEMENTS: // Get all agreements commands do not need any data.
        case DBCommandType::COUNT_AGREEMENTS: // Counts all existing agreements in the database. Does receive the same data as get all agreements to filter the count.
            if (command->data.type() != typeid(DBAgreementsQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_AGREEMENT: // Get agreement commands need the type, country, language, and optionally the version of the agreement (latest if not specified).
        case DBCommandType::DELETE_AGREEMENT: // Delete agreement commands need the type, country, language, and optionally the version (all versions if not specified).
            if (command->data.type() != typeid(DBAgreementQuery)) {
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

        case DBCommandType::INSERT_OR_UPDATE_AGREEMENT:
            // Inserts or updates an agreement document in the database.
            if (command->data.type() != typeid(DBAgreementData)) {
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

        case DBCommandType::INSERT_OR_UPDATE_FRIEND_REQUEST:
            // Inserts or updates a friend request in the database.
            if (command->data.type() != typeid(DBFriendRequestInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_SETTING:
            // Inserts or updates a server setting in the database.
            if (command->data.type() != typeid(DBInsertOrUpdateSettingQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_FILE:
            // Inserts or updates a file in the database.
            if (command->data.type() != typeid(DBInsertOrUpdateFileQuery)) {
                return false;
            }

            break;

        case DBCommandType::UPDATE_USER_PROFILE:
            // Updates a user profile in the database.
            if (command->data.type() != typeid(DBUserProfileUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_FRIEND: // Deletes a friendship between two users.
        case DBCommandType::UNBLOCK_FRIEND: // Unblocks a friend for a user.
            if (command->data.type() != typeid(DBFriendDeleteQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_OR_UPDATE_PERSISTENT_NOTIFICATION:
            // Inserts a persistent notification for a user.
            if (command->data.type() != typeid(DBPersistentNotificationInsertOrUpdateQuery)) {
                return false;
            }

            break;

        case DBCommandType::ADD_FRIEND:
            // Adds a friend for a user.
            if (command->data.type() != typeid(DBFriendshipInsertQuery)) {
                return false;
            }

            break;

        case DBCommandType::BLOCK_FRIEND:
            // Blocks a friend for a user.
            if (command->data.type() != typeid(DBBlockInsertQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_SIGNED_AGREEMENTS:
            // Gets all signed agreements for a user.
            if (command->data.type() != typeid(DBUserAgreementsQuery)) {
                return false;
            }

            break;

        case DBCommandType::LIST_DEVICES:
            // Lists devices with optional filtering and pagination.
            if (command->data.type() != typeid(DBDevicesListQuery)) {
                return false;
            }

            break;

        case DBCommandType::LIST_ACCOUNTS:
            // Lists accounts with optional filtering and pagination.
            if (command->data.type() != typeid(DBAccountsListQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_USER_DEVICE_ATTRIBUTE:
            // Deletes a specific device attribute for a user.
            if (command->data.type() != typeid(DBDeviceAttributeDeleteQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_DEVICE_ATTRIBUTES_FOR_OWNERSHIP:
            // Deletes all device attributes for a specific user-device ownership.
            if (command->data.type() != typeid(DBOwnershipDeleteQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_DEVICE:
            // Deletes a device.
            if (command->data.type() != typeid(DBIdQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_OWNERSHIP:
            // Deletes an ownership relationship.
            if (command->data.type() != typeid(DBOwnershipDeleteQuery)) {
                return false;
            }

            break;

        case DBCommandType::DELETE_USER_AGREEMENT:
            // Deletes a specific user agreement.
            if (command->data.type() != typeid(DBUserAgreementDeleteQuery)) {
                return false;
            }

            break;

        case DBCommandType::INACTIVATE_ALL_USER_OWNERSHIPS:
            // Inactivates all ownerships for a user.
            if (command->data.type() != typeid(DBPidQuery)) {
                return false;
            }

            break;

        case DBCommandType::COUNT_DEVICES:
            // Counts devices with optional filtering.
            if (command->data.type() != typeid(DBDevicesListQuery)) {
                return false;
            }

            break;

        case DBCommandType::COUNT_ACCOUNTS:
            // Counts accounts with optional filtering.
            if (command->data.type() != typeid(DBAccountsListQuery)) {
                return false;
            }

            break;

        case DBCommandType::INSERT_TASK:
            // Inserts a new pending task.
            if (command->data.type() != typeid(DBTaskInsertQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_ALL_TASKS:
            // Gets all pending tasks. No data required.
            break;

        case DBCommandType::DELETE_TASK:
            // Deletes a task by its ID.
            if (command->data.type() != typeid(DBIdQuery)) {
                return false;
            }

            break;

        case DBCommandType::UPLOAD_FESTIVAL_SCORE:
            // Uploads a festival score (sets team, optionally increments wins).
            if (command->data.type() != typeid(DBFestivalScoreUploadQuery)) {
                return false;
            }

            break;

        case DBCommandType::GET_FESTIVAL_TOTALS:
            // Gets the totals (user count and total wins) per team for a festival.
            if (command->data.type() != typeid(DBFestivalIdQuery)) {
                return false;
            }

            break;
    }

    return true;
}

} // namespace db
