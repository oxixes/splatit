#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

#include <utility>
#include <vector>
#include <any>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <thread>

#include <nlohmann/json.hpp>

#include "../logger.hpp"
#include "dbTypes.hpp"
#include "../util/promise.hpp"

using json = nlohmann::json;

namespace db {

enum class DBType {
    SQLITE3
};

enum class DBVersion {
    EMPTY = 0,
    INITIAL
};

const DBVersion CURRENT_VERSION = DBVersion::INITIAL;

enum class DBCommandType {
    GENERIC,
    GET_USER_BY_PID,
    GET_USER_BY_USERNAME,
    GET_GAME_SERVER_ACCESS,
    GET_USER_INFO,
    GET_FRIENDS_INFO,
    UPDATE_USER_INFO,
    GET_USER_PROFILE,
    GET_DEVICE_ATTRIBUTES
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

struct DBPidQuery {
    uint32_t pid;
};

struct DBUsernameQuery {
    std::string username;
};

struct DBGameServerAccessQuery {
    uint32_t pid;
    std::string serverId;
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
    std::string serverId;
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
    std::string friendUsername;
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
    uint32_t utcOffset;
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

class Result {
protected:
    std::any data;
    DBResultStatus status;

public:
    explicit Result(DBResultStatus status, std::any data) {
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

using PromisesQueue = std::queue<std::shared_ptr<Promise<std::unique_ptr<Result>>>>;

class Command {
protected:
    std::shared_ptr<PromisesQueue> promisesQueue = nullptr;
    std::shared_ptr<std::mutex> promisesMutex = nullptr;
    std::shared_ptr<std::condition_variable> promisesCV = nullptr;
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

class Database {
protected:
    explicit Database(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version);

    std::shared_ptr<Logger::Logger> logger;

    std::queue<std::pair<std::unique_ptr<Command>, std::shared_ptr<Promise<std::unique_ptr<Result>>>>> commandQueue;
    std::mutex commandQueueMutex;

    DBType dbType;
    DBVersion dbVersion;

    bool shouldStop = false;

    static bool verifyCommandArgs(const std::unique_ptr<Command>& command);
public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void close() = 0;

    virtual std::shared_ptr<Promise<std::unique_ptr<Result>>> queueCommand(std::unique_ptr<Command> command,
                                                                           std::shared_ptr<std::mutex> promisesMutex,
                                                                           std::shared_ptr<std::condition_variable> promisesCV,
                                                                           std::shared_ptr<PromisesQueue> promisesQueue) = 0;
    virtual void processQueue() = 0;
    virtual void waitForQueue() = 0;

    static std::unique_ptr<Command> craftVoidCommand(const std::string& command);
    static std::unique_ptr<Command> craftGetUserByPIDCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetUserByUsernameCommand(const std::string& username);
    static std::unique_ptr<Command> craftGetGameServerAccessCommand(uint32_t pid, const std::string& serverId);
    static std::unique_ptr<Command> craftGetUserInfoCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetFriendsInfoCommand(uint32_t pid);
    static std::unique_ptr<Command> craftUpdateUserInfoCommand(uint32_t pid, std::optional<bool> showOnline,
                                                               std::optional<bool> showPlaying,
                                                               std::optional<bool> blockRequests,
                                                               std::optional<std::vector<uint8_t>> nnaInfo,
                                                               std::optional<std::vector<uint8_t>> presence,
                                                               std::optional<std::vector<uint8_t>> comment,
                                                               std::optional<datetime_t> lastOnline);
    static std::unique_ptr<Command> craftGetUserProfileCommand(uint32_t pid);
    static std::unique_ptr<Command> craftGetDeviceAttributesCommand(uint32_t pid, uint32_t deviceId);

    static std::shared_ptr<Database> createDatabase(const json& config, const std::shared_ptr<Logger::Logger>& logger);

    std::shared_ptr<Promise<std::unique_ptr<Result>>> runCommand(std::unique_ptr<Command> command,
                           std::shared_ptr<std::mutex> promisesMutex,
                           std::shared_ptr<std::condition_variable> promisesCV,
                           std::shared_ptr<PromisesQueue> promisesQueue);

    [[nodiscard]] DBType getType() const;
    [[nodiscard]] DBVersion getVersion() const;
};

} // namespace db

#endif //SPLATOON_SERVER_DATABASE_HPP
