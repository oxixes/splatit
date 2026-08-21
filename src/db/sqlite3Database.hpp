#ifndef SPLATOON_SERVER_SQLITE3DATABASE_HPP
#define SPLATOON_SERVER_SQLITE3DATABASE_HPP

#include <sqlite3.h>

#include <filesystem>
#include <unordered_map>

#include "sqlDatabase.hpp"
#include "dbTypes.hpp"
#include "migrations/migrations.hpp"

namespace fs = std::filesystem;

namespace db {

class sqlite3Database final : public sqlDatabase {
public:
    sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath);
    ~sqlite3Database() override;

    std::shared_ptr<Database> createSession() override;

    bool init() override;
    void close() override;

protected:
    DBResultStatus execute(const std::string& sql,
                           const std::vector<DBDataType>& bindTypes,
                           const std::vector<std::shared_ptr<DBData>>& bindData,
                           const std::vector<DBDataType>& resultTypes,
                           const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData,
                           bool cacheStatement) override;
    DBResultStatus executeInsertReturningId(const std::string& sql,
                                            const std::vector<DBDataType>& bindTypes,
                                            const std::vector<std::shared_ptr<DBData>>& bindData,
                                            int64_t& insertedId) override;

    [[nodiscard]] bool inTransaction() const override;

    [[nodiscard]] std::string beginTransactionSQL(bool immediate) const override;
    [[nodiscard]] std::string greatestFunction() const override;
    [[nodiscard]] std::string caseInsensitiveLikeOperator() const override;
    [[nodiscard]] std::string backendName() const override;

private:
    sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath,
        std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue,
        std::shared_ptr<std::mutex> commandQueueMutex, std::shared_ptr<std::atomic<bool>> shouldStop,
        std::shared_ptr<std::thread> dbThreadHandle, std::shared_ptr<std::condition_variable> dbQueueCV,
        std::shared_ptr<std::mutex> queueWaitMutex, std::shared_ptr<std::condition_variable> dbQueueWaitCV,
        DBType dbType, DBVersion dbVersion); // For creating a session

    sqlite3* db = nullptr;
    fs::path dbPath;

    // Prepared statements are cached per SQL string so they are only prepared once
    std::unordered_map<std::string, sqlite3_stmt*> statementCache;

    bool craftStatement(const std::string& command, sqlite3_stmt** outStatement) const;
    bool bindValues(sqlite3_stmt* statement, const std::vector<DBDataType>& dataTypes,
                    const std::vector<std::shared_ptr<DBData>>& data) const;
    bool runStatement(sqlite3_stmt* statement, const std::vector<DBDataType>& dataTypes,
                      const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData) const;
};

} // namespace db

#endif //SPLATOON_SERVER_SQLITE3DATABASE_HPP
