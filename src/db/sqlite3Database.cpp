#include <memory>
#include <utility>
#include <chrono>
#include <date/date.h>

#include "sqlite3Database.hpp"
#include "../util/util.hpp"

namespace db {

sqlite3Database::sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath) :
        sqlDatabase(std::move(logger), DBType::SQLITE3, DBVersion::EMPTY) {
    this->dbPath = dbPath;
}

sqlite3Database::sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath,
        std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue,
        std::shared_ptr<std::mutex> commandQueueMutex, std::shared_ptr<std::atomic<bool>> shouldStop,
        std::shared_ptr<std::thread> dbThreadHandle, std::shared_ptr<std::condition_variable> dbQueueCV,
        std::shared_ptr<std::mutex> queueWaitMutex, std::shared_ptr<std::condition_variable> dbQueueWaitCV,
        DBType dbType, DBVersion dbVersion) :
        sqlDatabase(std::move(logger), std::move(commandQueue), std::move(commandQueueMutex), std::move(shouldStop),
                    std::move(dbThreadHandle), std::move(dbQueueCV), std::move(queueWaitMutex),
                    std::move(dbQueueWaitCV), dbType, dbVersion) {
    this->dbPath = dbPath;
}

sqlite3Database::~sqlite3Database() {
    close();
}

std::shared_ptr<Database> sqlite3Database::createSession() {
    auto sessionDb = std::shared_ptr<Database>(new sqlite3Database(
        logger, dbPath, commandQueue, commandQueueMutex, shouldStop, dbThreadHandle,
        dbQueueCV, queueWaitMutex, dbQueueWaitCV, dbType, dbVersion));

    if (!sessionDb->init()) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to initialize SQLite 3 session database: " + dbPath.string());
        throw std::runtime_error("Failed to initialize SQLite 3 session database");
    }

    logger->log(Logger::level::DEBUG, Logger::group::DB, "SQLite 3 session database created: " + dbPath.string());

    return sessionDb;
}

// It is assumed that if a session database is created, the main database is already initialized and still exists.
// Externally modifying the main database while the server runs, specifically deleting or renaming it, is not supported and will lead to undefined behavior.
bool sqlite3Database::init() {
    if (!isSession && !util::checkParentDirectory(dbPath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to open SQLite 3 database: parent directory does not exist");
        return false;
    }

    bool dbExists = isSession || fs::exists(dbPath);

    if (sqlite3_open_v2(dbPath.string().c_str(), &db,
                        SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to open SQLite 3 database: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    if (!isSession && dbExists) {
        try {
            dbVersion = obtainVersion();

            logger->log(Logger::level::DEBUG, Logger::group::DB, "DB version: "
                + std::to_string(static_cast<int>(dbVersion)));
        } catch (const std::runtime_error& e) {
            logger->log(Logger::level::FAILURE, Logger::group::DB,
                        "Failed to obtain database version: " + std::string(e.what()));
            return false;
        }
    }

    if (execute("PRAGMA foreign_keys = ON;", {}, {}, {}, nullptr, false) == DBResultStatus::SUCCESS) {
        logger->log(Logger::level::DEBUG, Logger::group::DB, "Foreign keys enabled for SQLite 3 database.");
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to enable foreign keys: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    if (execute("PRAGMA journal_mode = WAL;", {}, {}, {}, nullptr, false) == DBResultStatus::SUCCESS) {
        logger->log(Logger::level::DEBUG, Logger::group::DB, "WAL journal mode enabled for SQLite 3 database.");
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to set WAL journal mode: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    return true;
}

DBResultStatus sqlite3Database::execute(const std::string& sql,
                                        const std::vector<DBDataType>& bindTypes,
                                        const std::vector<std::shared_ptr<DBData>>& bindData,
                                        const std::vector<DBDataType>& resultTypes,
                                        const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData,
                                        bool cacheStatement) {
    sqlite3_stmt* statement = nullptr;
    bool cached = false;

    if (cacheStatement) {
        auto it = statementCache.find(sql);
        if (it != statementCache.end()) {
            statement = it->second;
            cached = true;
        }
    }

    if (statement == nullptr) {
        if (!craftStatement(sql, &statement)) {
            return DBResultStatus::FAILURE_STMT;
        }

        if (cacheStatement) {
            statementCache[sql] = statement;
            cached = true;
        }
    }

    if (!bindValues(statement, bindTypes, bindData)) {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        if (!cached) sqlite3_finalize(statement);
        return DBResultStatus::FAILURE_DATA;
    }

    bool ok = runStatement(statement, resultTypes, returnedData);

    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (!cached) sqlite3_finalize(statement);

    return ok ? DBResultStatus::SUCCESS : DBResultStatus::FAILURE_EXEC;
}

DBResultStatus sqlite3Database::executeInsertReturningId(const std::string& sql,
                                                         const std::vector<DBDataType>& bindTypes,
                                                         const std::vector<std::shared_ptr<DBData>>& bindData,
                                                         int64_t& insertedId) {
    DBResultStatus status = execute(sql, bindTypes, bindData, {}, nullptr, true);
    if (status != DBResultStatus::SUCCESS) return status;

    insertedId = sqlite3_last_insert_rowid(db);
    if (insertedId < 0) {
        return DBResultStatus::FAILURE_EXEC;
    }

    return DBResultStatus::SUCCESS;
}

bool sqlite3Database::inTransaction() const {
    return sqlite3_get_autocommit(db) == 0;
}

std::string sqlite3Database::beginTransactionSQL(bool immediate) const {
    return immediate ? "BEGIN IMMEDIATE;" : "BEGIN TRANSACTION;";
}

std::string sqlite3Database::greatestFunction() const {
    return "MAX";
}

std::string sqlite3Database::caseInsensitiveLikeOperator() const {
    return "LIKE";
}

std::string sqlite3Database::backendName() const {
    return "SQLite 3";
}

bool sqlite3Database::craftStatement(const std::string& command, sqlite3_stmt** outStatement) const {
    if (sqlite3_prepare_v2(db, command.c_str(), -1, outStatement, nullptr) != SQLITE_OK) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to craft SQLite 3 statement: " + std::string(sqlite3_errmsg(db)) +
                    " (command: " + command + ")");
        return false;
    }

    return true;
}

bool sqlite3Database::bindValues(sqlite3_stmt *statement, const std::vector<DBDataType>& dataTypes,
                                 const std::vector<std::shared_ptr<DBData>>& data) const {
    for (int i = 0; i < dataTypes.size(); i++) {
        int result = 0;
        switch (dataTypes[i]) {
            case DBDataType::INTEGER:
                result = sqlite3_bind_int64(statement, i + 1, std::any_cast<int64_t>(
                        std::dynamic_pointer_cast<DBInteger>(data[i])->data));
                break;
            case DBDataType::STRING:
                result = sqlite3_bind_text(statement, i + 1, std::any_cast<std::string>(
                        std::dynamic_pointer_cast<DBString>(data[i])->data).c_str(), -1, SQLITE_TRANSIENT);
                break;
            case DBDataType::BLOB:
                result = sqlite3_bind_blob64(statement, i + 1, std::any_cast<std::vector<uint8_t>>(
                                                     std::dynamic_pointer_cast<DBBlob>(data[i])->data).data(),
                                             std::any_cast<std::vector<uint8_t>>(std::dynamic_pointer_cast<DBBlob>(data[i])->data).size(),
                                             SQLITE_TRANSIENT);
                break;
            case DBDataType::DATETIME: {
                auto tp = std::any_cast<datetime_t>(std::dynamic_pointer_cast<DBDateTime>(data[i])->data);
                std::string date = std::format("{:%Y-%m-%d %H:%M:%S}", tp);
                result = sqlite3_bind_text(statement, i + 1, date.c_str(), -1, SQLITE_TRANSIENT);
                break;
            }
            case DBDataType::NULL_T:
                result = sqlite3_bind_null(statement, i + 1);
                break;
        }

        if (result != SQLITE_OK) {
            logger->log(Logger::level::FAILURE, Logger::group::DB,
                        "Failed to bind SQLite 3 statement data: " + std::string(sqlite3_errmsg(db)));
            return false;
        }
    }

    return true;
}

bool sqlite3Database::runStatement(sqlite3_stmt* statement, const std::vector<DBDataType>& dataTypes,
                                   const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData) const {
    if (statement == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::DB, "Failed to run SQLite 3 statement: statement is null");
        return false;
    }

    logger->log(Logger::level::DEBUG, Logger::group::DB, "Running SQLite 3 statement: " +
                                                         std::string(sqlite3_sql(statement)));

    int result = sqlite3_step(statement);
    while (result != SQLITE_DONE) {
        if (result == SQLITE_ROW && returnedData != nullptr && !dataTypes.empty()) {
            returnedData->emplace_back();
            for (int i = 0; i < sqlite3_column_count(statement) && i < dataTypes.size(); i++) {
                switch (dataTypes[i]) {
                    case DBDataType::INTEGER: {
                        if (sqlite3_column_type(statement, i) == SQLITE_NULL) {
                            returnedData->back().emplace_back(new DBNull());
                        } else {
                            // Use sqlite3_column_int64 to handle 64-bit integers
                            returnedData->back().emplace_back(new DBInteger(sqlite3_column_int64(statement, i)));
                        }
                        break;
                    }
                    case DBDataType::STRING: {
                        if (sqlite3_column_type(statement, i) == SQLITE_NULL) {
                            returnedData->back().emplace_back(new DBNull());
                        } else {
                            returnedData->back().emplace_back(new DBString(std::string(
                                    reinterpret_cast<const char *>(sqlite3_column_text(statement, i)))));
                        }
                        break;
                    }
                    case DBDataType::BLOB: {
                        if (sqlite3_column_type(statement, i) == SQLITE_NULL) {
                            returnedData->back().emplace_back(new DBNull());
                        } else {
                            auto blob = std::vector<uint8_t>();
                            blob.resize(sqlite3_column_bytes(statement, i));
                            memcpy(blob.data(), sqlite3_column_blob(statement, i), blob.size());
                            returnedData->back().emplace_back(new DBBlob(blob));
                        }
                        break;
                    }
                    case DBDataType::DATETIME: {
                        if (sqlite3_column_type(statement, i) == SQLITE_NULL) {
                            returnedData->back().emplace_back(new DBNull());
                        } else {
                            std::string datetimeStr(reinterpret_cast<const char *>(sqlite3_column_text(statement, i)));
                            std::istringstream ss(datetimeStr);
                            date::sys_seconds tp;
                            ss >> date::parse("%Y-%m-%d %H:%M:%S", tp);
                            if (ss.fail()) {
                                logger->log(Logger::level::FAILURE, Logger::group::DB,
                                            "Failed to parse datetime: " + datetimeStr);
                                returnedData->back().emplace_back(new DBDateTime(datetime_t{}));
                            } else {
                                returnedData->back().emplace_back(new DBDateTime(tp));
                            }
                        }
                        break;
                    }
                    case DBDataType::NULL_T:
                        returnedData->back().emplace_back(new DBNull());
                        break;
                }
            }
        } else if (result != SQLITE_ROW) {
            logger->log(Logger::level::FAILURE, Logger::group::DB,
                        "Failed to run SQLite 3 statement: " + std::string(sqlite3_errmsg(db)));
            return false;
        }

        result = sqlite3_step(statement);
    }

    return true;
}

void sqlite3Database::close() {
    if (db == nullptr) return;

    shutdownQueueThread();

    for (const auto& [sql, statement] : statementCache) {
        sqlite3_finalize(statement);
    }
    statementCache.clear();

    sqlite3_close(db);
    db = nullptr;
}

} // namespace db
