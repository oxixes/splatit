#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

#include <utility>
#include <vector>
#include <any>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

#include <nlohmann/json.hpp>

#include "../logger.hpp"
#include "dbTypes.hpp"
#include "../util/manualTask.hpp"

using json = nlohmann::json;

namespace db {

enum class DBType {
    SQLITE3
};

enum class SystemType {
    ACCOUNTS,
    FRIENDS_AUTH,
    FRIENDS_SECURE,
    SPLATOON_AUTH,
    BOSS
};

enum class DBVersion {
    EMPTY = 0,
    INITIAL
};

constexpr DBVersion CURRENT_VERSION = DBVersion::INITIAL;

enum class DBCommandType {
    GENERIC,
    GET_USER_BY_PID,
    GET_USER_BY_USERNAME,
    GET_GAME_SERVER_ACCESS,
    GET_USER_INFO_BY_PID,
    GET_USER_INFO_BY_USERNAME,
    GET_FRIENDS_INFO,
    GET_FRIEND_REQUEST,
    GET_SENT_FRIEND_REQUESTS,
    GET_RECEIVED_FRIEND_REQUESTS,
    GET_BLOCKED_FRIENDS,
    GET_USER_PROFILE,
    GET_USER_MII,
    GET_USER_EMAIL,
    GET_DEVICE_ATTRIBUTES,
    GET_ALL_AGREEMENTS,
    GET_AGREEMENT,
    GET_DEVICE,
    GET_OWNERSHIP,
    GET_LATEST_OWNERSHIP,
    HAS_ACTIVE_OWNERSHIP,
    GET_OWNERSHIPS,
    GET_PERSISTENT_NOTIFICATIONS,
    GET_SIGNED_AGREEMENTS,
    GET_SETTING,
    GET_FILE,
    LIST_DEVICES,
    LIST_ACCOUNTS,
    INSERT_USER_INFO,
    ADD_FRIEND,
    BLOCK_FRIEND,
    INSERT_GAME_SERVER_ACCESS,
    INSERT_OR_UPDATE_PERSISTENT_NOTIFICATION,
    INSERT_OR_UPDATE_DEVICE,
    INSERT_OR_UPDATE_USER_AGREEMENT,
    INSERT_OR_UPDATE_AGREEMENT,
    INSERT_OR_UPDATE_MII,
    INSERT_OR_UPDATE_EMAIL,
    INSERT_USER_PROFILE,
    INSERT_OR_UPDATE_DEVICE_ATTRIBUTES,
    INSERT_OR_UPDATE_OWNERSHIP,
    INSERT_OR_UPDATE_FRIEND_REQUEST,
    INSERT_OR_UPDATE_SETTING,
    INSERT_OR_UPDATE_FILE,
    UPDATE_USER_INFO,
    UPDATE_USER_PROFILE,
    INACTIVATE_DEVICE_OWNERSHIPS,
    INACTIVATE_ALL_USER_OWNERSHIPS,
    DELETE_MII,
    DELETE_EMAIL,
    DELETE_USER,
    DELETE_USER_OWNERSHIPS,
    DELETE_USER_AGREEMENTS,
    DELETE_USER_DEVICE_ATTRIBUTES,
    DELETE_USER_DEVICE_ATTRIBUTE,
    DELETE_DEVICE_ATTRIBUTES_FOR_OWNERSHIP,
    DELETE_DEVICE,
    DELETE_DEVICE_OWNERSHIPS,
    DELETE_DEVICE_ATTRIBUTES,
    DELETE_OWNERSHIP,
    DELETE_USER_AGREEMENT,
    DELETE_AGREEMENT,
    DELETE_FRIEND,
    DELETE_FRIEND_REQUEST,
    DELETE_ALL_BLOCKS_BY_PID,
    DELETE_ALL_FRIEND_REQUESTS_BY_PID,
    DELETE_ALL_FRIENDSHIPS_BY_PID,
    DELETE_ALL_NOTIFICATIONS_BY_PID,
    DELETE_USER_INFO_BY_PID,
    DELETE_PERSISTENT_NOTIFICATION,
    DELETE_GAME_SERVER_ACCESS,
    UNBLOCK_FRIEND,
    COUNT_AGREEMENTS,
    COUNT_DEVICES,
    COUNT_ACCOUNTS,
    INSERT_TASK,
    GET_ALL_TASKS,
    DELETE_TASK
};

enum class DBResultStatus {
    SUCCESS,
    FAILURE_GENERIC,
    FAILURE_ARGS,
    FAILURE_STMT,
    FAILURE_DATA,
    FAILURE_EXEC
};

struct DBGenericCommand {
    std::string cmd;
    std::vector<DBDataType> bindTypes;
    std::vector<std::shared_ptr<DBData>> bindData;
    std::vector<DBDataType> resultTypes;
};

struct DBIdQuery {
    int64_t id;
};

struct DBPidQuery {
    uint32_t pid;
};

struct DBUsernameQuery {
    std::string username;
};

struct DBGameServerAccessQuery {
    uint32_t pid;
};

struct DBUserInfoUpdate {
    uint32_t pid{};
    std::optional<std::string> username;
    std::optional<bool> showPresence;
    std::optional<bool> showPlaying;
    std::optional<bool> blockRequests;
    std::optional<std::vector<uint8_t>> nnaInfo;
    std::optional<std::vector<uint8_t>> presence;
    std::optional<std::vector<uint8_t>> comment;
    std::optional<datetime_t> lastOnline;
};

struct DBDeviceAttributesQuery {
    uint32_t pid;
    uint32_t deviceId;
};

struct DBAgreementsQuery {
    std::optional<std::string> type;
    std::optional<std::string> country;
    std::optional<std::string> language;
    std::optional<int> version;

    std::vector<std::pair<std::string, bool>> sortBy{};

    uint64_t pageSize{};
    uint64_t pageNumber{};
};

struct DBAgreementQuery {
    std::string type;
    std::string country;
    std::string language;
    std::optional<int> version;
};

struct DBOwnershipQuery {
    uint32_t pid;
    uint32_t deviceId;
};

struct DBDeviceInsertOrUpdateQuery {
    uint32_t deviceId;
    std::string language;
    uint32_t platformId;
    uint32_t region;
    std::string serialNumber;
    std::string systemVersion;
    std::string type;
    std::string updatedBy;
    std::string status;
    bool banned;
    datetime_t lastUpdated;
};

struct DBUserAgreementInsertOrUpdateQuery {
    uint32_t pid;
    std::string type;
    int version;
    std::string country;
    datetime_t signedAt;
};


struct DBMiiInsertOrUpdateQuery {
    std::optional<int64_t> miiId;
    std::string hash;
    std::string name;
    bool primary;
    std::string data;
};

struct DBEmailInsertOrUpdateQuery {
    std::optional<int64_t> emailId;
    std::string email;
    bool parent;
    bool primary;
    bool reachable;
    std::string type;
    std::string updatedBy;
    bool validated;
    datetime_t validatedAt;
    std::string validationCode;
};

struct DBUserProfileInsertQuery {
    std::string username;
    std::string password;
    int64_t emailId;
    int64_t miiId;
    bool gender;
    int64_t region;
    std::string tz;
    std::string language;
    bool active;
    bool marketing;
    bool offDevice;
    std::string birthdate;
    std::string country;
    datetime_t created;
    datetime_t updated;
};

struct DBFriendshipInsertQuery {
    uint32_t pid{};
    uint32_t friendPid{};
    datetime_t becameFriends;
};

struct DBBlockInsertQuery {
    uint32_t pid;
    uint32_t blockedPid;
    datetime_t createdAt;
    std::vector<uint8_t> gameKey;
};

struct DBPersistentNotificationInsertOrUpdateQuery {
    std::optional<int64_t> id;
    uint32_t forPid;
    int64_t value1;
    uint32_t value2;
    uint32_t value3;
    uint32_t value4;
    std::string text;
};

struct DBUserProfileUpdateQuery {
    uint32_t pid{};
    std::optional<std::string> username;
    std::optional<std::string> password;
    std::optional<int64_t> emailId;
    std::optional<int64_t> miiId;
    std::optional<bool> gender;
    std::optional<int64_t> region;
    std::optional<std::string> tz;
    std::optional<std::string> language;
    std::optional<bool> active;
    std::optional<bool> marketing;
    std::optional<bool> offDevice;
    std::optional<std::string> birthdate;
    std::optional<std::string> country;
    std::optional<datetime_t> created;
    std::optional<datetime_t> updated;
};

struct DBDeviceAttributesInsertOrUpdateQuery {
    uint32_t deviceId;
    uint32_t pid;
    std::string name;
    std::string value;
    datetime_t createdDate;
};

struct DBDeviceAttributeDeleteQuery {
    uint32_t deviceId;
    uint32_t pid;
    std::string name;
};

struct DBDevicesListQuery {
    std::optional<uint32_t> platform;
    std::optional<uint32_t> region;
    std::optional<bool> banned;
    std::optional<std::string> serialNumber;
    std::optional<std::string> type;
    std::vector<std::pair<std::string, bool>> sortBy{};
    uint64_t pageSize{};
    uint64_t pageNumber{};
};

struct DBAccountsListQuery {
    std::optional<std::string> username;
    std::optional<bool> gender;
    std::optional<uint64_t> region;
    std::optional<bool> active;
    std::vector<std::pair<std::string, bool>> sortBy{};
    uint64_t pageSize{};
    uint64_t pageNumber{};
};

struct DBUserAgreementsQuery {
    uint32_t pid;
};

struct DBOwnershipDeleteQuery {
    uint32_t pid;
    uint32_t deviceId;
};

struct DBUserAgreementDeleteQuery {
    uint32_t pid;
    std::string type;
    int version;
    std::string country;
};

struct DBOwnershipInsertOrUpdateQuery {
    uint32_t pid;
    uint32_t deviceId;
    std::string status;
    datetime_t lastUpdated;
};

struct DBFriendRequestInsertOrUpdateQuery {
    std::optional<int64_t> id;
    uint32_t fromPid;
    uint32_t toPid;
    datetime_t expiresAt;
    datetime_t createdAt;
    std::vector<uint8_t> data;
};

struct DBFriendDeleteQuery {
    uint32_t pid;
    uint32_t friendPid;
};

struct DBGetSettingQuery {
    std::string key;
};

struct DBInsertOrUpdateSettingQuery {
    std::string key;
    std::string value;
};

struct DBGetFileQuery {
    std::string hash;
};

struct DBInsertOrUpdateFileQuery {
    std::string hash;
    std::vector<uint8_t> data;
};

struct DBGenericResult {
    std::vector<std::vector<std::shared_ptr<DBData>>> data;
};

struct DBUserData {
    uint32_t pid;
    std::string username;
    std::string password;
};

struct DBGameServerAccessData {
    uint32_t pid;
    std::string password;
};

struct DBUserInfoData {
    uint32_t pid;
    std::string username;
    bool showPresence;
    bool showPlaying;
    bool blockRequests;
    std::vector<uint8_t> nnaInfo;
    std::vector<uint8_t> presence;
    std::vector<uint8_t> comment;
    datetime_t lastOnline;
};

struct DBFriendInfoData {
    uint32_t friendPid;
    bool showPresence;
    bool showPlaying;
    bool blockRequests;
    std::vector<uint8_t> nnaInfo;
    std::vector<uint8_t> presence;
    std::vector<uint8_t> comment;
    datetime_t lastOnline;
    datetime_t becameFriends;
};

struct DBFriendRequestData {
    int64_t id;
    uint32_t fromPid;
    uint32_t toPid;
    datetime_t expiresAt;
    datetime_t createdAt;
    std::vector<uint8_t> data;
    std::optional<std::vector<uint8_t>> nnaInfo;
};

struct DBBlockData {
    uint32_t pid;
    uint32_t blockedPid;
    datetime_t createdAt;
    std::vector<uint8_t> gameKey;
    std::vector<uint8_t> nnaInfo;
};

struct DBPersistentNotificationData {
    int64_t id;
    uint32_t forPid;
    int64_t value1;
    uint32_t value2;
    uint32_t value3;
    uint32_t value4;
    std::string text;
};

struct DBUserProfileData {
    uint32_t pid;
    std::string username;
    int64_t emailId;
    int64_t miiId;
    bool gender;
    int64_t region;
    std::string tz;
    std::string language;
    bool active;
    bool marketing;
    bool offDevice;
    std::string birthdate;
    std::string country;
    datetime_t created;
    datetime_t updated;
    std::string email;
    bool emailParent;
    bool emailPrimary;
    bool emailReachable;
    std::string emailType;
    std::string emailUpdatedBy;
    bool emailValidated;
    datetime_t emailValidatedDate;
    std::string emailValidationCode;
    std::string miiName;
    std::string miiData;
    bool miiPrimary;
    std::string miiHash;
};

struct DBUserMii {
    std::string username;
    int64_t miiId;
    std::string miiName;
    std::string miiData;
    bool miiPrimary;
    std::string miiHash;
};

struct DBUserEmail {
    int64_t emailId;
    std::string email;
    bool emailParent;
    bool emailPrimary;
    bool emailReachable;
    std::string emailType;
    std::string emailUpdatedBy;
    bool emailValidated;
    datetime_t emailValidatedDate;
    std::string emailValidationCode;
};

struct DBDeviceAttributeData {
    uint32_t pid;
    uint32_t deviceId;
    std::string name;
    std::string value;
    datetime_t createdDate;
};

struct DBAgreementData {
    std::string type;
    int version;
    std::string country;
    std::string language;
    std::string languageName;
    datetime_t publishedAt;
    std::string mainTitle;
    std::string subTitle;
    std::string agreeText;
    std::string disagreeText;
    std::string mainText;
    std::string subText;
};

struct DBDeviceData {
    uint32_t deviceId;
    std::string language;
    uint32_t platformId;
    uint32_t region;
    std::string serialNumber;
    std::string systemVersion;
    std::string type;
    std::string updatedBy;
    std::string status;
    bool banned;
    datetime_t lastUpdated;
};

struct DBOwnershipData {
    uint32_t pid;
    uint32_t deviceId;
    std::string status;
    datetime_t lastUpdated;
};

struct DBTaskInsertQuery {
    int type;
    std::string params;
};

struct DBTaskData {
    int64_t id;
    int type;
    std::string params;
};

struct DBUserAgreementData {
    std::string type;
    int version;
    std::string country;
    datetime_t signedAt;
};

class Result {
protected:
    std::any data;
    DBResultStatus status;

public:
    Result(const DBResultStatus status, std::any&& data) {
        this->status = status;
        this->data = std::move(data);
    }

    [[nodiscard]] DBResultStatus getStatus() const {
        return status;
    }

    [[nodiscard]] bool hasData() const {
        return data.has_value();
    }

    template<typename T>
    [[nodiscard]] T getData() const {
        if (!data.has_value()) {
            throw std::runtime_error("No data available in Result");
        }
        return std::any_cast<T>(data);
    }

    [[nodiscard]] std::any getRawData() const {
        return data;
    }

    friend class Database;
};

class Database;

class Command {
protected:
    std::shared_ptr<async::ManualTask<Result>> task;
    std::shared_ptr<Database> db;
    DBCommandType type;
    std::any data;

public:
    friend class Database;
    friend class sqlite3Database;

    explicit Command(DBCommandType type, std::any data) {
        this->type = type;
        this->data = std::move(data);
    }
};

class Database : public std::enable_shared_from_this<Database> {
protected:
    Database(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version);

    std::shared_ptr<Logger::Logger> logger;

    std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue =
        std::make_shared<std::queue<std::unique_ptr<Command>>>();
    std::shared_ptr<std::mutex> commandQueueMutex = std::make_shared<std::mutex>();

    DBType dbType;
    DBVersion dbVersion;

    std::shared_ptr<std::atomic<bool>> shouldStop = std::make_shared<std::atomic<bool>>(false);

    static bool verifyCommandArgs(const std::unique_ptr<Command>& command);
public:
    virtual ~Database() = default;
    virtual std::shared_ptr<Database> createSession() = 0;

    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void close() = 0;

    virtual async::ManualTask<Result> startTransaction(bool immediate = false) = 0;
    virtual async::ManualTask<Result> commitTransaction() = 0;
    virtual async::ManualTask<Result> rollbackTransaction() = 0;

    virtual async::ManualTask<Result> queueCommand(std::unique_ptr<Command> command) = 0;
    virtual void processQueue() = 0;
    virtual void waitForQueue() = 0;

    static std::unique_ptr<Command> craftVoidCommand(const std::string& command);
    static std::unique_ptr<Command> craftGetUserByPIDCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserByUsernameCommand(const std::string& username);
    static std::unique_ptr<Command> craftGetGameServerAccessCommand(uint32_t pid);
    static std::unique_ptr<Command> craftInsertGameServerAccessCommand(uint32_t pid, const std::string& password);
    static std::unique_ptr<Command> craftGetUserInfoByPidCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserInfoByUsernameCommand(const std::string& username);
    static std::unique_ptr<Command> craftInsertUserInfoCommand(uint32_t pid, const std::string& username,
                                                               bool showOnline, bool showPlaying,
                                                               bool blockRequests, const std::vector<uint8_t>& nnaInfo,
                                                               const std::vector<uint8_t>& presence,
                                                               const std::vector<uint8_t>& comment,
                                                               datetime_t lastOnline);
    static std::unique_ptr<Command> craftAddFriendCommand(uint32_t pid, uint32_t friendPid,
                                                          datetime_t becameFriends);
    static std::unique_ptr<Command> craftBlockFriendCommand(uint32_t pid, uint32_t blockedPid,
                                                            datetime_t createdAt, const std::vector<uint8_t>& gameKey);
    static std::unique_ptr<Command> craftUpdateUserInfoCommand(uint32_t pid, std::optional<std::string> username,
                                                               std::optional<bool> showOnline,
                                                               std::optional<bool> showPlaying,
                                                               std::optional<bool> blockRequests,
                                                               std::optional<std::vector<uint8_t>> nnaInfo,
                                                               std::optional<std::vector<uint8_t>> presence,
                                                               std::optional<std::vector<uint8_t>> comment,
                                                               std::optional<datetime_t> lastOnline);
    static std::unique_ptr<Command> craftGetFriendsInfoCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetFriendRequestCommand(int64_t id);
    static std::unique_ptr<Command> craftGetSentFriendRequestsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetReceivedFriendRequestsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetBlockedFriendsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserProfileCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserMiiCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserEmailCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetDeviceAttributesCommand(uint32_t pid, uint32_t deviceId);
    static std::unique_ptr<Command> craftGetAllAgreementsCommand(std::optional<std::string> type = std::nullopt,
                                                                std::optional<std::string> country = std::nullopt,
                                                                std::optional<std::string> language = std::nullopt,
                                                                std::optional<int> version = std::nullopt,
                                                                std::vector<std::pair<std::string, bool>> sortBy = {{"type", true}, {"version", true}},
                                                                uint64_t pageSize = std::numeric_limits<uint64_t>::max(),
                                                                uint64_t pageNumber = 0);
    static std::unique_ptr<Command> craftGetAgreementCommand(const std::string& type, const std::string& country,
                                                             const std::string& language, std::optional<int> version = std::nullopt);
    static std::unique_ptr<Command> craftGetDeviceCommand(uint32_t deviceId);
    static std::unique_ptr<Command> craftGetOwnershipCommand(uint32_t pid, uint32_t deviceId);
    static std::unique_ptr<Command> craftGetPersistentNotificationsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetLatestOwnershipCommand(uint32_t pid);
    static std::unique_ptr<Command> craftHasActiveOwnershipCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetOwnershipsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetSignedAgreementsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetSettingCommand(const std::string& key);
    static std::unique_ptr<Command> craftGetFileCommand(const std::string& hash);
    static std::unique_ptr<Command> craftListDevicesCommand(std::optional<uint32_t> platform = std::nullopt,
                                                           std::optional<uint32_t> region = std::nullopt,
                                                           std::optional<bool> banned = std::nullopt,
                                                           std::optional<std::string> serialNumber = std::nullopt,
                                                           std::optional<std::string> type = std::nullopt,
                                                           std::vector<std::pair<std::string, bool>> sortBy = {{"id", false}},
                                                           uint64_t pageSize = std::numeric_limits<uint64_t>::max(),
                                                           uint64_t pageNumber = 0);
    static std::unique_ptr<Command> craftListAccountsCommand(std::optional<std::string> username = std::nullopt,
                                                            std::optional<bool> gender = std::nullopt,
                                                            std::optional<uint64_t> region = std::nullopt,
                                                            std::optional<bool> active = std::nullopt,
                                                            std::vector<std::pair<std::string, bool>> sortBy = {{"pid", false}},
                                                            uint64_t pageSize = std::numeric_limits<uint64_t>::max(),
                                                            uint64_t pageNumber = 0);
    static std::unique_ptr<Command> craftCountDevicesCommand(std::optional<uint32_t> platform = std::nullopt,
                                                            std::optional<uint32_t> region = std::nullopt,
                                                            std::optional<bool> banned = std::nullopt,
                                                            std::optional<std::string> serialNumber = std::nullopt,
                                                            std::optional<std::string> type = std::nullopt);
    static std::unique_ptr<Command> craftCountAccountsCommand(std::optional<std::string> username = std::nullopt,
                                                             std::optional<bool> gender = std::nullopt,
                                                             std::optional<uint64_t> region = std::nullopt,
                                                             std::optional<bool> active = std::nullopt);
    static std::unique_ptr<Command> craftInactivateDeviceOwnershipsCommand(uint32_t deviceId);
    static std::unique_ptr<Command> craftInsertOrUpdatePersistentNotificationCommand(std::optional<int64_t>id, uint32_t forPid,
                                                                           int64_t value1, uint32_t value2, uint32_t value3,
                                                                           uint32_t value4, const std::string& text);
    static std::unique_ptr<Command> craftInsertOrUpdateDeviceCommand(uint32_t deviceId, const std::string& language,
                                                                     uint32_t platformId, uint32_t region,
                                                                     const std::string& serialNumber,
                                                                     const std::string& systemVersion,
                                                                     const std::string& type,
                                                                     const std::string& updatedBy,
                                                                     const std::string& status,
                                                                     bool banned,
                                                                     datetime_t lastUpdated);
    static std::unique_ptr<Command> craftInsertOrUpdateUserAgreementCommand(uint32_t pid, const std::string& type,
                                                                            int version, const std::string& country,
                                                                            datetime_t signedAt);
    static std::unique_ptr<Command> craftInsertOrUpdateAgreementCommand(const std::string& type, int version,
                                                                        const std::string& country,
                                                                        const std::string& language,
                                                                        const std::string& languageName,
                                                                        datetime_t publishedAt,
                                                                        const std::string& mainTitle,
                                                                        const std::string& subTitle,
                                                                        const std::string& agreeText,
                                                                        const std::string& disagreeText,
                                                                        const std::string& mainText,
                                                                        const std::string& subText);
    static std::unique_ptr<Command> craftInsertOrUpdateMiiCommand(std::optional<int64_t> miiId,
                                                                  const std::string& hash, const std::string& name,
                                                                  bool primary, const std::string& data);
    static std::unique_ptr<Command> craftInsertOrUpdateEmailCommand(std::optional<int64_t> emailId,
                                                                    const std::string& email, bool parent,
                                                                    bool primary, bool reachable,
                                                                    const std::string& type,
                                                                    const std::string& updatedBy,
                                                                    bool validated, datetime_t validatedAt,
                                                                    const std::string& validationCode);
    static std::unique_ptr<Command> craftInsertProfileCommand(const std::string& username,
                                                              const std::string& password, int64_t emailId,
                                                              int64_t miiId, bool gender, int64_t region,
                                                              const std::string& tz, const std::string& language,
                                                              bool active, bool marketing, bool offDevice,
                                                              const std::string& birthdate, const std::string& country,
                                                              datetime_t created, datetime_t updated);
    static std::unique_ptr<Command> craftInsertOrUpdateDeviceAttributesCommand(uint32_t deviceId, uint32_t pid,
                                                                               const std::string& name,
                                                                               const std::string& value,
                                                                               datetime_t createdDate);
    static std::unique_ptr<Command> craftInsertOrUpdateOwnershipCommand(uint32_t pid, uint32_t deviceId,
                                                                        const std::string& status,
                                                                        datetime_t lastUpdated);
    static std::unique_ptr<Command> craftInsertOrUpdateFriendRequestCommand(std::optional<int64_t> id, uint32_t fromPid,
                                                                      uint32_t toPid, datetime_t expiresAt,
                                                                      datetime_t createdAt, const std::vector<uint8_t>& data);
    static std::unique_ptr<Command> craftUpdateUserProfileCommand(uint32_t pid, std::optional<std::string> username,
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
                                                                  std::optional<datetime_t> updated);
    static std::unique_ptr<Command> craftInsertOrUpdateSettingCommand(const std::string& key, const std::string& value);
    static std::unique_ptr<Command> craftInsertOrUpdateFileCommand(const std::string& hash, const std::vector<uint8_t>& data);
    static std::unique_ptr<Command> craftDeleteMiiCommand(int64_t miiId);
    static std::unique_ptr<Command> craftDeleteEmailCommand(int64_t emailId);
    static std::unique_ptr<Command> craftDeleteUserCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteUserOwnershipsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteUserAgreementsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteUserDeviceAttributesCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteUserDeviceAttributeCommand(uint32_t pid, uint32_t deviceId, const std::string& name);
    static std::unique_ptr<Command> craftDeleteDeviceAttributesForOwnershipCommand(uint32_t pid, uint32_t deviceId);
    static std::unique_ptr<Command> craftDeleteDeviceCommand(uint32_t deviceId);
    static std::unique_ptr<Command> craftDeleteDeviceOwnershipsCommand(uint32_t deviceId);
    static std::unique_ptr<Command> craftDeleteDeviceAttributesCommand(uint32_t deviceId);
    static std::unique_ptr<Command> craftDeleteOwnershipCommand(uint32_t pid, uint32_t deviceId);
    static std::unique_ptr<Command> craftDeleteUserAgreementCommand(uint32_t pid, const std::string& type, int version, const std::string& country);
    static std::unique_ptr<Command> craftInactivateAllUserOwnershipsCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteAgreementCommand(const std::string& type, const std::string& country,
                                                                const std::string& language, std::optional<int> version = std::nullopt);
    static std::unique_ptr<Command> craftDeleteFriendCommand(uint32_t pid, uint32_t friendPid);
    static std::unique_ptr<Command> craftDeleteFriendRequestCommand(int64_t id);
    static std::unique_ptr<Command> craftDeleteAllBlocksByPidCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteAllFriendRequestsByPidCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteAllFriendshipsByPidCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteAllNotificationsByPidCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeleteUserInfoByPidCommand(uint32_t pid);
    static std::unique_ptr<Command> craftDeletePersistentNotificationCommand(int64_t id);
    static std::unique_ptr<Command> craftDeleteGameServerAccessCommand(uint32_t pid);
    static std::unique_ptr<Command> craftUnblockFriendCommand(uint32_t pid, uint32_t blockedPid);
    static std::unique_ptr<Command> craftCountAgreementsCommand(std::optional<std::string> type = std::nullopt,
                                                                std::optional<std::string> country = std::nullopt,
                                                                std::optional<std::string> language = std::nullopt,
                                                                std::optional<int> version = std::nullopt);
    static std::unique_ptr<Command> craftInsertTaskCommand(int type, const std::string& params);
    static std::unique_ptr<Command> craftGetAllTasksCommand();
    static std::unique_ptr<Command> craftDeleteTaskCommand(int64_t id);

    static std::shared_ptr<Database> createDatabase(const json& config, const std::shared_ptr<Logger::Logger>& logger);

    async::ManualTask<Result> runCommand(std::unique_ptr<Command> command);

    [[nodiscard]] DBType getType() const;
    [[nodiscard]] DBVersion getVersion() const;
};

} // namespace db

#endif //SPLATOON_SERVER_DATABASE_HPP
