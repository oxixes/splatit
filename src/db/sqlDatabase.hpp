#ifndef SPLATOON_SERVER_SQLDATABASE_HPP
#define SPLATOON_SERVER_SQLDATABASE_HPP

#include <thread>
#include <mutex>
#include <condition_variable>

#include "database.hpp"
#include "dbTypes.hpp"

namespace db {

// Intermediate base class for SQL-based backends (SQLite 3, PostgreSQL).
// It implements the command queue/thread machinery and the whole command
// processing logic once, on top of a small set of backend primitives that
// each concrete backend implements with its native client library.
class sqlDatabase : public Database {
public:
    bool run() override;

    async::ManualTask<Result> startTransaction(bool immediate = false) override;
    async::ManualTask<Result> commitTransaction() override;
    async::ManualTask<Result> rollbackTransaction() override;

    async::ManualTask<Result> queueCommand(std::unique_ptr<Command> command) override;
    void processQueue() override;
    void waitForQueue() override;

protected:
    sqlDatabase(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version);
    sqlDatabase(std::shared_ptr<Logger::Logger> logger,
                std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue,
                std::shared_ptr<std::mutex> commandQueueMutex, std::shared_ptr<std::atomic<bool>> shouldStop,
                std::shared_ptr<std::thread> dbThreadHandle, std::shared_ptr<std::condition_variable> dbQueueCV,
                std::shared_ptr<std::mutex> queueWaitMutex, std::shared_ptr<std::condition_variable> dbQueueWaitCV,
                DBType dbType, DBVersion dbVersion); // For creating a session

    std::shared_ptr<std::thread> dbThreadHandle;
    std::shared_ptr<std::condition_variable> dbQueueCV;

    std::shared_ptr<std::mutex> queueWaitMutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::condition_variable> dbQueueWaitCV = std::make_shared<std::condition_variable>();

    bool isSession = false;

    void dbThread() const;
    void processCommand(const std::unique_ptr<Command>& command);

    // Stops the database thread (if running) and drains the command queue.
    // Called by the concrete backends' close() implementations.
    void shutdownQueueThread();

    // Obtains the stored database version by querying the db_info table.
    // Throws std::runtime_error on failure.
    DBVersion obtainVersion();

    // Backend primitives. execute() prepares (and optionally caches) the
    // statement for the given SQL, binds the given data, runs it and returns
    // the resulting rows (parsed according to resultTypes) in returnedData
    // (if not null). SQL strings use '?' placeholders; backends translate
    // them to their native placeholder syntax if needed.
    virtual DBResultStatus execute(const std::string& sql,
                                   const std::vector<DBDataType>& bindTypes,
                                   const std::vector<std::shared_ptr<DBData>>& bindData,
                                   const std::vector<DBDataType>& resultTypes,
                                   const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData,
                                   bool cacheStatement) = 0;

    // Runs an INSERT statement (which must not include the auto-generated id
    // column) and returns the id assigned to the inserted row.
    virtual DBResultStatus executeInsertReturningId(const std::string& sql,
                                                    const std::vector<DBDataType>& bindTypes,
                                                    const std::vector<std::shared_ptr<DBData>>& bindData,
                                                    int64_t& insertedId) = 0;

    [[nodiscard]] virtual bool inTransaction() const = 0;

    // Dialect hooks
    [[nodiscard]] virtual std::string beginTransactionSQL(bool immediate) const = 0;
    [[nodiscard]] virtual std::string greatestFunction() const = 0; // MAX (SQLite) / GREATEST (PostgreSQL)
    [[nodiscard]] virtual std::string caseInsensitiveLikeOperator() const = 0; // LIKE (SQLite) / ILIKE (PostgreSQL)
    [[nodiscard]] virtual std::string backendName() const = 0; // For log messages
};

} // namespace db

#endif //SPLATOON_SERVER_SQLDATABASE_HPP
