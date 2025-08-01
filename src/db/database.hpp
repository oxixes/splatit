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
#include "../util/task.hpp"

using json = nlohmann::json;

namespace db {

enum class DBType {
    SQLITE3
};

enum class SystemType {
    ACCOUNTS,
    FRIENDS_AUTH,
    FRIENDS_SECURE,
    SPLATOON_AUTH
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
    INSERT_GAME_SERVER_ACCESS,
    GET_USER_INFO,
    INSERT_USER_INFO,
    UPDATE_USER_INFO,
    GET_FRIENDS_INFO,
    GET_USER_PROFILE,
    GET_DEVICE_ATTRIBUTES,
    GET_AGREEMENT,
    GET_DEVICE,
    GET_LATEST_PID,
    GET_OWNERSHIP,
    GET_LATEST_OWNERSHIP,
    HAS_ACTIVE_OWNERSHIP,
    INSERT_OR_UPDATE_DEVICE,
    INSERT_OR_UPDATE_USER_AGREEMENT,
    INSERT_OR_UPDATE_MII,
    INSERT_OR_UPDATE_EMAIL,
    INSERT_USER_PROFILE,
    INSERT_OR_UPDATE_DEVICE_ATTRIBUTES,
    INSERT_OR_UPDATE_OWNERSHIP,
    UPDATE_USER_PROFILE,
    DELETE_MII,
    DELETE_EMAIL
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

struct DBGetAgreementQuery {
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
};

struct DBUserProfileInsertQuery {
    uint32_t pid;
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

struct DBOwnershipInsertOrUpdateQuery {
    uint32_t pid;
    uint32_t deviceId;
    std::string status;
    datetime_t lastUpdated;
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
    std::string miiName;
    std::string miiData;
    bool miiPrimary;
    std::string miiHash;
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
    datetime_t lastUpdated;
};

struct DBOwnershipData {
    uint32_t pid;
    uint32_t deviceId;
    std::string status;
    datetime_t lastUpdated;
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

class Command {
protected:
    async::Task<Result>::promise_type* promise;
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
    explicit Database(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version);

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

    virtual async::Task<Result> startTransaction() = 0;
    virtual async::Task<Result> commitTransaction() = 0;
    virtual async::Task<Result> rollbackTransaction() = 0;

    virtual async::Task<Result> queueCommand(std::unique_ptr<Command> command) = 0;
    virtual void processQueue() = 0;
    virtual void waitForQueue() = 0;

    static std::unique_ptr<Command> craftVoidCommand(const std::string& command);
    static std::unique_ptr<Command> craftGetUserByPIDCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserByUsernameCommand(const std::string& username);
    static std::unique_ptr<Command> craftGetGameServerAccessCommand(uint32_t pid);
    static std::unique_ptr<Command> craftInsertGameServerAccessCommand(uint32_t pid, const std::string& password);
    static std::unique_ptr<Command> craftGetUserInfoCommand(uint32_t pid);
    static std::unique_ptr<Command> craftInsertUserInfoCommand(uint32_t pid, bool showOnline, bool showPlaying,
                                                               bool blockRequests, const std::vector<uint8_t>& nnaInfo,
                                                               const std::vector<uint8_t>& presence,
                                                               const std::vector<uint8_t>& comment,
                                                               datetime_t lastOnline);
    static std::unique_ptr<Command> craftUpdateUserInfoCommand(uint32_t pid, std::optional<bool> showOnline,
                                                               std::optional<bool> showPlaying,
                                                               std::optional<bool> blockRequests,
                                                               std::optional<std::vector<uint8_t>> nnaInfo,
                                                               std::optional<std::vector<uint8_t>> presence,
                                                               std::optional<std::vector<uint8_t>> comment,
                                                               std::optional<datetime_t> lastOnline);
    static std::unique_ptr<Command> craftGetFriendsInfoCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserProfileCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetDeviceAttributesCommand(uint32_t pid, uint32_t deviceId);
    static std::unique_ptr<Command> craftGetAgreementCommand(const std::string& type, const std::string& country,
                                                             const std::string& language, std::optional<int> version = std::nullopt);
    static std::unique_ptr<Command> craftGetDeviceCommand(uint32_t deviceId);
    static std::unique_ptr<Command> craftGetLatestPidCommand();
    static std::unique_ptr<Command> craftGetOwnershipCommand(uint32_t pid, uint32_t deviceId);
    static std::unique_ptr<Command> craftGetLatestOwnershipCommand(uint32_t pid);
    static std::unique_ptr<Command> craftHasActiveOwnershipCommand(uint32_t pid);
    static std::unique_ptr<Command> craftInsertOrUpdateDeviceCommand(uint32_t deviceId, const std::string& language,
                                                                     uint32_t platformId, uint32_t region,
                                                                     const std::string& serialNumber,
                                                                     const std::string& systemVersion,
                                                                     const std::string& type,
                                                                     const std::string& updatedBy,
                                                                     datetime_t lastUpdated);
    static std::unique_ptr<Command> craftInsertOrUpdateUserAgreementCommand(uint32_t pid, const std::string& type,
                                                                            int version, const std::string& country,
                                                                            datetime_t signedAt);
    static std::unique_ptr<Command> craftInsertOrUpdateMiiCommand(std::optional<int64_t> miiId,
                                                                  const std::string& hash, const std::string& name,
                                                                  bool primary, const std::string& data);
    static std::unique_ptr<Command> craftInsertOrUpdateEmailCommand(std::optional<int64_t> emailId,
                                                                    const std::string& email, bool parent,
                                                                    bool primary, bool reachable,
                                                                    const std::string& type,
                                                                    const std::string& updatedBy,
                                                                    bool validated, datetime_t validatedAt);
    static std::unique_ptr<Command> craftInsertProfileCommand(uint32_t pid, const std::string& username,
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
    static std::unique_ptr<Command> craftDeleteMiiCommand(int64_t miiId);
    static std::unique_ptr<Command> craftDeleteEmailCommand(int64_t emailId);

    static std::shared_ptr<Database> createDatabase(const json& config, const std::shared_ptr<Logger::Logger>& logger);

    async::Task<Result> runCommand(std::unique_ptr<Command> command);

    [[nodiscard]] DBType getType() const;
    [[nodiscard]] DBVersion getVersion() const;
};

} // namespace db

#endif //SPLATOON_SERVER_DATABASE_HPP
