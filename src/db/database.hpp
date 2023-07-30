#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

#include <utility>
#include <vector>
#include <any>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <nlohmann/json.hpp>

#include "../logger.hpp"
#include "dbTypes.hpp"

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
    GET_GAME_SERVER_ACCESS
};

enum class DBResultStatus {
    SUCCESS,
    FAILURE_GENERIC,
    FAILURE_ARGS,
    FAILURE_STMT,
    FAILURE_DATA,
    FAILURE_EXEC
};

class Command {
public:
    DBCommandType type;
    std::vector<std::any> data;
    uint32_t commandId = 0;
    bool hasMutex = false;

    explicit Command(DBCommandType type, std::vector<std::any> data) {
        this->type = type;
        this->data = std::move(data);
    }
};

class Result {
public:
    std::vector<std::any> data;
    DBResultStatus status;
    uint32_t commandId;

    explicit Result(uint32_t commandId, DBResultStatus status, std::vector<std::any>& data) {
        this->commandId = commandId;
        this->status = status;
        this->data = std::move(data);
    }
};

class Database {
protected:
    explicit Database(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version);

    std::shared_ptr<Logger::Logger> logger;

    std::queue<std::unique_ptr<Command>> commandQueue;
    std::mutex commandQueueMutex;

    std::vector<std::unique_ptr<Result>> results;
    std::mutex resultsMutex;

    std::vector<std::tuple<uint32_t, std::unique_ptr<std::mutex>, std::unique_ptr<std::condition_variable>, std::thread::id>> commandCVs;
    std::mutex commandCVsMutex;

    uint32_t commandId = 0;

    DBType dbType;
    DBVersion dbVersion;

    static bool verifyCommandArgs(const std::unique_ptr<Command>& command);
public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void close() = 0;

    virtual uint32_t queueCommand(std::unique_ptr<Command> command, bool commandMutex) = 0;
    virtual void processQueue() = 0;
    virtual void waitForCommand(uint32_t commandId, std::shared_ptr<bool> shouldEnd) = 0;
    virtual void waitForQueue(std::shared_ptr<bool> shouldEnd) = 0;
    virtual void clearCommandMutex(uint32_t commandId) = 0;
    virtual void notifyCommand(uint32_t commandId) = 0;
    virtual void notifyQueue() = 0;

    std::unique_ptr<Result> getResult(uint32_t commandID);

    static std::unique_ptr<Command> craftVoidCommand(const std::string& command);
    static std::unique_ptr<Command> craftGetUserByPIDCommand(int pid);
    static std::unique_ptr<Command> craftGetUserByUsernameCommand(const std::string& username);
    static std::unique_ptr<Command> craftGetGameServerAccessCommand(int pid, const std::string& serverId);

    static std::shared_ptr<Database> createDatabase(const json& config, std::shared_ptr<Logger::Logger> logger);

    static uint32_t runCommand(std::shared_ptr<Database> db, std::unique_ptr<Command> command,
                           const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                           const std::function<void(unsigned int)>& unregisterCloseCall, bool& shouldStop);

    [[nodiscard]] DBType getType() const;
    [[nodiscard]] DBVersion getVersion() const;
};

} // namespace db

#endif //SPLATOON_SERVER_DATABASE_HPP
