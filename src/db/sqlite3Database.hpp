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

    std::shared_ptr<Promise> queueCommand(std::unique_ptr<Command> command,
                                          std::shared_ptr<std::mutex> promisesMutex,
                                          std::shared_ptr<std::condition_variable> promisesCV,
                                          std::shared_ptr<std::queue<std::shared_ptr<Promise>>> promisesQueue) override;
    void processQueue() override;
    void waitForQueue() override;

private:
    sqlite3* db = nullptr;
    fs::path dbPath;

    std::thread dbThreadHandle;
    std::condition_variable dbQueueCV;

    std::mutex queueWaitMutex;
    std::condition_variable dbQueueWaitCV;

    sqlite3_stmt* getUserByPIDStatement = nullptr;
    sqlite3_stmt* getUserByUsernameStatement = nullptr;
    sqlite3_stmt* getGameServerAccessStatement = nullptr;
    sqlite3_stmt* insertGameServerAccessStatement = nullptr;
    sqlite3_stmt* getUserInfoStatement = nullptr;
    sqlite3_stmt* insertUserInfoStatement = nullptr;
    sqlite3_stmt* getFriendsInfoStatement = nullptr;
    sqlite3_stmt* getUserProfileStatement = nullptr;
    sqlite3_stmt* getDeviceAttributesStatement = nullptr;

    void dbThread();

    void processCommand(const std::pair<std::unique_ptr<Command>, std::shared_ptr<Promise>>& command);
    bool craftStatement(const std::string& command, sqlite3_stmt** outStatement);
    bool bindData(sqlite3_stmt* statement, const std::vector<DBDataType>& dataTypes,
                  const std::vector<std::shared_ptr<DBData>>& data);
    bool runStatement(sqlite3_stmt* statement, const std::vector<DBDataType>& dataTypes,
                      const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData);

    DBVersion obtainVersion();
};

} // namespace db

#endif //SPLATOON_SERVER_SQLITE3DATABASE_HPP
