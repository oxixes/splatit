#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

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
    NO_DATA = 0,
    INITIAL
};

const DBVersion CURRENT_VERSION = DBVersion::INITIAL;

enum class DBCommandType {
    GENERIC
};

enum class DBResultStatus {
    SUCCESS,
    FAILURE_GENERIC
};

class Command {
public:
    DBCommandType type;
    std::vector<std::any> data;
    int commandId = 0;
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
    int commandId;

    explicit Result(int commandId, DBResultStatus status, std::vector<std::any>& data) {
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

    std::vector<std::tuple<int, std::unique_ptr<std::mutex>, std::unique_ptr<std::condition_variable>, std::thread::id>> commandCVs;
    std::mutex commandCVsMutex;

    int commandId = 0;

    DBType dbType;
    DBVersion dbVersion;
public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void close() = 0;

    virtual int queueCommand(std::unique_ptr<Command> command, bool commandMutex) = 0;
    virtual void processQueue() = 0;
    virtual void waitForCommand(int commandId, std::shared_ptr<bool> shouldEnd) = 0;
    virtual void waitForQueue(std::shared_ptr<bool> shouldEnd) = 0;
    virtual void clearCommandMutex(int commandId) = 0;
    virtual void notifyCommand(int commandId) = 0;
    virtual void notifyQueue() = 0;

    std::unique_ptr<Result> getResult(int commandID);

    static std::unique_ptr<Command> craftVoidCommand(const std::string& command);

    static std::shared_ptr<Database> createDatabase(const json& config, std::shared_ptr<Logger::Logger> logger);

    [[nodiscard]] DBType getType() const;
    [[nodiscard]] DBVersion getVersion() const;
};

} // namespace db

#endif //SPLATOON_SERVER_DATABASE_HPP
