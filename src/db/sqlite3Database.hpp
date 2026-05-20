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

    async::ManualTask<Result> startTransaction(bool immediate = false) override;
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
    sqlite3_stmt* getUserInfoByPidStatement = nullptr;
    sqlite3_stmt* getUserInfoByUsernameStatement = nullptr;
    sqlite3_stmt* insertUserInfoStatement = nullptr;
    sqlite3_stmt* getFriendsInfoStatement = nullptr;
    sqlite3_stmt* getFriendRequestStatement = nullptr;
    sqlite3_stmt* getSentFriendRequestsStatement = nullptr;
    sqlite3_stmt* getReceivedFriendRequestsStatement = nullptr;
    sqlite3_stmt* getBlockedFriendsStatement = nullptr;
    sqlite3_stmt* getUserProfileStatement = nullptr;
    sqlite3_stmt* getUserMiiStatement = nullptr;
    sqlite3_stmt* getUserEmailStatement = nullptr;
    sqlite3_stmt* getDeviceAttributesStatement = nullptr;
    sqlite3_stmt* getAgreementStatement = nullptr;
    sqlite3_stmt* getDeviceStatement = nullptr;
    sqlite3_stmt* getOwnershipStatement = nullptr;
    sqlite3_stmt* getLatestOwnershipStatement = nullptr;
    sqlite3_stmt* getSettingStatement = nullptr;
    sqlite3_stmt* getFileStatement = nullptr;
    sqlite3_stmt* hasActiveOwnershipStatement = nullptr;
    sqlite3_stmt* getOwnershipsStatement = nullptr;
    sqlite3_stmt* getPersistentNotificationsStatement = nullptr;
    sqlite3_stmt* inactivateDeviceOwnershipsStatement = nullptr;
    sqlite3_stmt* getLatestAgreementStatement = nullptr;
    sqlite3_stmt* addFriendStatement = nullptr;
    sqlite3_stmt* blockFriendStatement = nullptr;
    sqlite3_stmt* insertPersistentNotificationStatement = nullptr;
    sqlite3_stmt* insertOrUpdateDeviceStatement = nullptr;
    sqlite3_stmt* insertOrUpdateUserAgreementStatement = nullptr;
    sqlite3_stmt* insertOrUpdateAgreementStatement = nullptr;
    sqlite3_stmt* insertOrUpdateMiiStatement = nullptr;
    sqlite3_stmt* insertOrUpdateEmailStatement = nullptr;
    sqlite3_stmt* insertProfileStatement = nullptr;
    sqlite3_stmt* insertOrUpdateDeviceAttributesStatement = nullptr;
    sqlite3_stmt* insertOrUpdateOwnershipStatement = nullptr;
    sqlite3_stmt* insertOrUpdateFriendRequestStatement = nullptr;
    sqlite3_stmt* insertOrUpdateSettingStatement = nullptr;
    sqlite3_stmt* insertOrUpdateFileStatement = nullptr;
    sqlite3_stmt* deleteMiiStatement = nullptr;
    sqlite3_stmt* deleteEmailStatement = nullptr;
    sqlite3_stmt* deleteUserStatement = nullptr;
    sqlite3_stmt* deleteUserOwnershipsStatement = nullptr;
    sqlite3_stmt* deleteUserAgreementsStatement = nullptr;
    sqlite3_stmt* deleteUserDeviceAttributesStatement = nullptr;
    sqlite3_stmt* deleteAgreementStatement = nullptr;
    sqlite3_stmt* deleteAgreementVersionStatement = nullptr;
    sqlite3_stmt* deleteFriendStatement = nullptr;
    sqlite3_stmt* deleteFriendRequestStatement = nullptr;
    sqlite3_stmt* deletePersistentNotificationStatement = nullptr;
    sqlite3_stmt* deleteAttributeStatement = nullptr;
    sqlite3_stmt* deleteAttributesStatement = nullptr;
    sqlite3_stmt* deleteDeviceStatement = nullptr;
    sqlite3_stmt* deleteDeviceOwnershipsStatement = nullptr;
    sqlite3_stmt* deleteDeviceAttributesStatement = nullptr;
    sqlite3_stmt* deleteOwnershipStatement = nullptr;
    sqlite3_stmt* deleteUserAgreementStatement = nullptr;
    sqlite3_stmt* deleteGameServerAccessStatement = nullptr;
    sqlite3_stmt* inactivateOwnershipsStatement = nullptr;
    sqlite3_stmt* unblockFriendStatement = nullptr;
    sqlite3_stmt* deleteAllBlocksByPidStatement = nullptr;
    sqlite3_stmt* deleteAllFriendRequestsByPidStatement = nullptr;
    sqlite3_stmt* deleteAllFriendshipsByPidStatement = nullptr;
    sqlite3_stmt* deleteAllNotificationsByPidStatement = nullptr;
    sqlite3_stmt* deleteUserInfoByPidStatement = nullptr;
    sqlite3_stmt* insertTaskStatement = nullptr;
    sqlite3_stmt* getAllTasksStatement = nullptr;
    sqlite3_stmt* deleteTaskStatement = nullptr;

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
