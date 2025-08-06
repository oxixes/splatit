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

    std::shared_ptr<Database> createSession() override;

    bool init() override;
    bool run() override;
    void close() override;

    async::ManualTask<Result> startTransaction() override;
    async::ManualTask<Result> commitTransaction() override;
    async::ManualTask<Result> rollbackTransaction() override;

    async::ManualTask<Result> queueCommand(std::unique_ptr<Command> command) override;
    void processQueue() override;
    void waitForQueue() override;

private:
    sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath,
        std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue,
        std::shared_ptr<std::mutex> commandQueueMutex, std::shared_ptr<std::atomic<bool>> shouldStop,
        std::shared_ptr<std::thread> dbThreadHandle, std::shared_ptr<std::condition_variable> dbQueueCV,
        std::shared_ptr<std::mutex> queueWaitMutex, std::shared_ptr<std::condition_variable> dbQueueWaitCV,
        DBType dbType, DBVersion dbVersion); // For creating a session

    sqlite3* db = nullptr;
    fs::path dbPath;

    std::shared_ptr<std::thread> dbThreadHandle;
    std::shared_ptr<std::condition_variable> dbQueueCV;

    std::shared_ptr<std::mutex> queueWaitMutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::condition_variable> dbQueueWaitCV = std::make_shared<std::condition_variable>();

    sqlite3_stmt* getUserByPIDStatement = nullptr;
    sqlite3_stmt* getUserByUsernameStatement = nullptr;
    sqlite3_stmt* getGameServerAccessStatement = nullptr;
    sqlite3_stmt* insertGameServerAccessStatement = nullptr;
    sqlite3_stmt* getUserInfoStatement = nullptr;
    sqlite3_stmt* insertUserInfoStatement = nullptr;
    sqlite3_stmt* getFriendsInfoStatement = nullptr;
    sqlite3_stmt* getUserProfileStatement = nullptr;
    sqlite3_stmt* getDeviceAttributesStatement = nullptr;
    sqlite3_stmt* getAgreementStatement = nullptr;
    sqlite3_stmt* getDeviceStatement = nullptr;
    sqlite3_stmt* getLatestPIDStatement = nullptr;
    sqlite3_stmt* getOwnershipStatement = nullptr;
    sqlite3_stmt* getLatestOwnershipStatement = nullptr;
    sqlite3_stmt* hasActiveOwnershipStatement = nullptr;
    sqlite3_stmt* getLatestAgreementStatement = nullptr;
    sqlite3_stmt* insertOrUpdateDeviceStatement = nullptr;
    sqlite3_stmt* insertOrUpdateUserAgreementStatement = nullptr;
    sqlite3_stmt* insertOrUpdateMiiStatement = nullptr;
    sqlite3_stmt* insertOrUpdateEmailStatement = nullptr;
    sqlite3_stmt* insertProfileStatement = nullptr;
    sqlite3_stmt* insertOrUpdateDeviceAttributesStatement = nullptr;
    sqlite3_stmt* insertOrUpdateOwnershipStatement = nullptr;
    sqlite3_stmt* deleteMiiStatement = nullptr;
    sqlite3_stmt* deleteEmailStatement = nullptr;

    bool isSession = false;

    void dbThread() const;

    void processCommand(const std::unique_ptr<Command>& command);
    bool craftStatement(const std::string& command, sqlite3_stmt** outStatement) const;
    bool bindData(sqlite3_stmt* statement, const std::vector<DBDataType>& dataTypes,
                  const std::vector<std::shared_ptr<DBData>>& data) const;
    bool runStatement(sqlite3_stmt* statement, const std::vector<DBDataType>& dataTypes,
                      const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData) const;

    DBVersion obtainVersion();
};

} // namespace db

#endif //SPLATOON_SERVER_SQLITE3DATABASE_HPP
