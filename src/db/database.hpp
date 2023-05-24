#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

#include <vector>
#include <any>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "../logger.hpp"

namespace db {

enum class type {
    SQLITE3
};

enum class commandType {
    GENERIC
};

enum class resultStatus {
    SUCCESS,
    FAILURE_GENERIC
};

class Command {
public:
    commandType type;
    std::vector<std::any> data;
    int commandId = 0;
    bool hasMutex = false;

    explicit Command(commandType type, std::vector<std::any> data) {
        this->type = type;
        this->data = std::move(data);
    }
};

class Result {
public:
    std::vector<std::any> data;
    resultStatus status;
    int commandId;

    explicit Result(int commandId, resultStatus status, std::vector<std::any>& data) {
        this->commandId = commandId;
        this->status = status;
        this->data = std::move(data);
    }
};

class Database {
protected:
    explicit Database(std::shared_ptr<Logger::Logger> logger);

    std::shared_ptr<Logger::Logger> logger;

    std::queue<Command*> commandQueue;
    std::mutex commandQueueMutex;

    std::vector<Result*> results;
    std::mutex resultsMutex;

    std::vector<std::tuple<int, std::mutex*, std::condition_variable*, std::thread::id>> commandCVs;
    std::mutex commandCVsMutex;

    int commandId = 0;

public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void close() = 0;

    virtual int queueCommand(Command* command, bool commandMutex) = 0;
    virtual void processQueue() = 0;
    virtual void waitForCommand(int commandId, bool* shouldEnd) = 0;
    virtual void waitForQueue(bool* shouldEnd) = 0;
    virtual void clearCommandMutex(int commandId) = 0;
};

} // namespace db

#endif //SPLATOON_SERVER_DATABASE_HPP
