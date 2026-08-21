#ifndef SPLATOON_SERVER_POSTGRESDATABASE_HPP
#define SPLATOON_SERVER_POSTGRESDATABASE_HPP

#include <libpq-fe.h>

#include <unordered_map>

#include "sqlDatabase.hpp"
#include "dbTypes.hpp"
#include "migrations/migrations.hpp"

namespace db {

class postgresDatabase final : public sqlDatabase {
public:
    postgresDatabase(std::shared_ptr<Logger::Logger> logger, const std::string& host, uint16_t port,
                     const std::string& user, const std::string& password, const std::string& dbName);
    ~postgresDatabase() override;

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
    postgresDatabase(std::shared_ptr<Logger::Logger> logger, const std::string& host, uint16_t port,
        const std::string& user, const std::string& password, const std::string& dbName,
        std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue,
        std::shared_ptr<std::mutex> commandQueueMutex, std::shared_ptr<std::atomic<bool>> shouldStop,
        std::shared_ptr<std::thread> dbThreadHandle, std::shared_ptr<std::condition_variable> dbQueueCV,
        std::shared_ptr<std::mutex> queueWaitMutex, std::shared_ptr<std::condition_variable> dbQueueWaitCV,
        DBType dbType, DBVersion dbVersion); // For creating a session

    PGconn* conn = nullptr;

    std::string host;
    uint16_t port = 0;
    std::string portString;
    std::string user;
    std::string password;
    std::string dbName;

    // Prepared statements are cached per SQL string so they are only prepared once.
    // The map value is the server-side prepared statement name.
    std::unordered_map<std::string, std::string> statementCache;

    // Converts '?' placeholders into PostgreSQL's positional $N placeholders.
    static std::string convertPlaceholders(const std::string& sql);

    // Fills the libpq parameter arrays from the given bind data. Returns false if the data is invalid.
    bool prepareParameters(const std::vector<DBDataType>& bindTypes,
                           const std::vector<std::shared_ptr<DBData>>& bindData,
                           std::vector<std::string>& paramStorage,
                           std::vector<const char*>& paramValues,
                           std::vector<int>& paramLengths,
                           std::vector<int>& paramFormats) const;

    // Parses the rows of a PGresult into DBData rows according to the given data types.
    bool parseResult(PGresult* result, const std::vector<DBDataType>& resultTypes,
                     const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData) const;
};

} // namespace db

#endif //SPLATOON_SERVER_POSTGRESDATABASE_HPP
