#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

#include <vector>
#include <any>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "../logger.hpp"

enum class dbType {
    SQLITE3
};

enum class dbCommandType {
    GENERIC
};

enum class dbResultStatus {
    SUCCESS,
    FAILURE_GENERIC
};

class DBCommand {
public:
    dbCommandType type;
    std::vector<std::any> data;
    int commandId = 0;
    bool hasMutex = false;

    explicit DBCommand(dbCommandType type, std::vector<std::any> data) {
        this->type = type;
        this->data = std::move(data);
    }
};

class DBResult {
public:
    std::vector<std::any> data;
    dbResultStatus status;
    int commandId;

    explicit DBResult(int commandId, dbResultStatus status, std::vector<std::any>& data) {
        this->commandId = commandId;
        this->status = status;
        this->data = std::move(data);
    }
};

class Database {
protected:
    explicit Database(Logger::Logger* logger);

    Logger::Logger* logger;

    std::queue<DBCommand*> commandQueue;
    std::mutex commandQueueMutex;

    std::vector<DBResult*> results;
    std::mutex resultsMutex;

    std::vector<std::tuple<int, std::mutex*, std::condition_variable*, std::thread::id>> commandCVs;
    std::mutex commandCVsMutex;

    int commandId = 0;

public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void close() = 0;

    virtual int queueCommand(DBCommand* command, bool commandMutex) = 0;
    virtual void processQueue() = 0;
    virtual void waitForCommand(int commandId, bool* shouldEnd) = 0;
    virtual void waitForQueue(bool* shouldEnd) = 0;
    virtual void clearCommandMutex(int commandId) = 0;
};

#endif //SPLATOON_SERVER_DATABASE_HPP
