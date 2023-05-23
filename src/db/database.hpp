#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

#include <vector>
#include <any>
#include <queue>
#include <mutex>

#include "../logger.hpp"

enum class dbType {
    SQLITE3
};

enum class dbCommandType {
    GENERIC
};

class DBCommand {
public:
    dbCommandType type;
    std::vector<std::any> data;
    int commandId = 0;

    explicit DBCommand(dbCommandType type, std::vector<std::any> data) {
        this->type = type;
        this->data = std::move(data);
    }
};

class DBResult {
public:
    std::vector<std::any> data;
    int commandId = 0;

    explicit DBResult(std::vector<std::any> data) {
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

    int commandId = 0;

public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual bool run() = 0;
    virtual void close() = 0;

    virtual int queueCommand(DBCommand* command) = 0;
    virtual void processQueue() = 0;
    virtual void waitForCommand(int commandId) = 0;
    virtual void waitForQueue() = 0;
};

#endif //SPLATOON_SERVER_DATABASE_HPP
