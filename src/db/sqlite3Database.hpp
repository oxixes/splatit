#ifndef SPLATOON_SERVER_SQLITE3DATABASE_HPP
#define SPLATOON_SERVER_SQLITE3DATABASE_HPP

#include <sqlite3.h>

#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "database.hpp"
#include "dbTypes.hpp"
#include "migrations/migrations.hpp"
#include "../util/util.hpp"

namespace fs = std::filesystem;

namespace db {

class sqlite3Database final : public Database {
public:
    sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath);
    ~sqlite3Database() override;

    bool init() override;
    bool run() override;
    void close() override;

    int queueCommand(std::unique_ptr<Command> command, bool commandMutex) override;
    void processQueue() override;
    void waitForCommand(int commandId, std::shared_ptr<bool> shouldEnd) override;
    void waitForQueue(std::shared_ptr<bool> shouldEnd) override;
    void clearCommandMutex(int commandId) override;
    void notifyCommand(int commandId) override;
    void notifyQueue() override;

private:
    sqlite3* db = nullptr;
    fs::path dbPath;

    bool running = false;
    std::thread dbThreadHandle;
    std::mutex dbThreadMutex;
    std::condition_variable dbThreadCV;

    sqlite3_stmt* getUserByPIDStatement = nullptr;
    sqlite3_stmt* getUserByUsernameStatement = nullptr;
    sqlite3_stmt* getGameServerAccessStatement = nullptr;

    void dbThread();

    void processCommand(const std::unique_ptr<Command>& command);
    bool craftStatement(const std::string& command, sqlite3_stmt** outStatement);
    bool bindData(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                  const std::vector<std::shared_ptr<DBData>>& data);
    bool runStatement(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                      const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData);

    DBVersion obtainVersion();
};

} // namespace db

#endif //SPLATOON_SERVER_SQLITE3DATABASE_HPP
