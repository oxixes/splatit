#include <memory>
#include <utility>
#include <chrono>
#include <format>
#include <sstream>
#include <cctype>
#include <date/date.h>

#include "postgresDatabase.hpp"

namespace db {

postgresDatabase::postgresDatabase(std::shared_ptr<Logger::Logger> logger, const std::string& host, uint16_t port,
                                   const std::string& user, const std::string& password, const std::string& dbName) :
        sqlDatabase(std::move(logger), DBType::POSTGRESQL, DBVersion::EMPTY) {
    this->host = host;
    this->port = port;
    this->portString = std::to_string(port);
    this->user = user;
    this->password = password;
    this->dbName = dbName;
}

postgresDatabase::postgresDatabase(std::shared_ptr<Logger::Logger> logger, const std::string& host, uint16_t port,
        const std::string& user, const std::string& password, const std::string& dbName,
        std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue,
        std::shared_ptr<std::mutex> commandQueueMutex, std::shared_ptr<std::atomic<bool>> shouldStop,
        std::shared_ptr<std::thread> dbThreadHandle, std::shared_ptr<std::condition_variable> dbQueueCV,
        std::shared_ptr<std::mutex> queueWaitMutex, std::shared_ptr<std::condition_variable> dbQueueWaitCV,
        DBType dbType, DBVersion dbVersion) :
        sqlDatabase(std::move(logger), std::move(commandQueue), std::move(commandQueueMutex), std::move(shouldStop),
                    std::move(dbThreadHandle), std::move(dbQueueCV), std::move(queueWaitMutex),
                    std::move(dbQueueWaitCV), dbType, dbVersion) {
    this->host = host;
    this->port = port;
    this->portString = std::to_string(port);
    this->user = user;
    this->password = password;
    this->dbName = dbName;
}

postgresDatabase::~postgresDatabase() {
    close();
}

std::shared_ptr<Database> postgresDatabase::createSession() {
    auto sessionDb = std::shared_ptr<Database>(new postgresDatabase(
        logger, host, port, user, password, dbName,
        commandQueue, commandQueueMutex, shouldStop, dbThreadHandle,
        dbQueueCV, queueWaitMutex, dbQueueWaitCV, dbType, dbVersion));

    if (!sessionDb->init()) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to initialize PostgreSQL session database: " + dbName + "@" + host);
        throw std::runtime_error("Failed to initialize PostgreSQL session database");
    }

    logger->log(Logger::level::DEBUG, Logger::group::DB, "PostgreSQL session database created: " + dbName + "@" + host);

    return sessionDb;
}

bool postgresDatabase::init() {
    const char* keywords[] = {"host", "port", "user", "password", "dbname", nullptr};
    const char* values[] = {host.c_str(), portString.c_str(), user.c_str(), password.c_str(), dbName.c_str(), nullptr};

    conn = PQconnectdbParams(keywords, values, 0);
    if (PQstatus(conn) != CONNECTION_OK) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to open PostgreSQL database: " + std::string(PQerrorMessage(conn)));
        PQfinish(conn);
        conn = nullptr;
        return false;
    }

    // Ensure datetimes and blobs come back in the formats we parse
    if (execute("SET datestyle TO ISO;", {}, {}, {}, nullptr, false) != DBResultStatus::SUCCESS ||
        execute("SET bytea_output = 'hex';", {}, {}, {}, nullptr, false) != DBResultStatus::SUCCESS) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to configure PostgreSQL session: " + std::string(PQerrorMessage(conn)));
        PQfinish(conn);
        conn = nullptr;
        return false;
    }

    if (!isSession) {
        // Unlike SQLite, there is no database file whose existence tells us whether the
        // database has been initialized, so check for the db_info table instead.
        auto results = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();
        if (execute("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = current_schema() "
                    "AND table_name = 'db_info';", {}, {}, {DBDataType::INTEGER}, results, false)
                != DBResultStatus::SUCCESS) {
            logger->log(Logger::level::FAILURE, Logger::group::DB,
                        "Failed to check PostgreSQL database initialization state");
            PQfinish(conn);
            conn = nullptr;
            return false;
        }

        bool dbExists = !results->empty() && !results->at(0).empty() &&
                        std::any_cast<int64_t>(results->at(0).at(0)->data) > 0;

        if (dbExists) {
            try {
                dbVersion = obtainVersion();

                logger->log(Logger::level::DEBUG, Logger::group::DB, "DB version: "
                    + std::to_string(static_cast<int>(dbVersion)));
            } catch (const std::runtime_error& e) {
                logger->log(Logger::level::FAILURE, Logger::group::DB,
                            "Failed to obtain database version: " + std::string(e.what()));
                PQfinish(conn);
                conn = nullptr;
                return false;
            }
        }
    }

    return true;
}

std::string postgresDatabase::convertPlaceholders(const std::string& sql) {
    std::string converted;
    converted.reserve(sql.size() + 16);

    int index = 0;
    bool inStringLiteral = false;
    for (const char c : sql) {
        if (c == '\'') {
            inStringLiteral = !inStringLiteral;
        }

        if (c == '?' && !inStringLiteral) {
            converted += '$';
            converted += std::to_string(++index);
        } else {
            converted += c;
        }
    }

    return converted;
}

bool postgresDatabase::prepareParameters(const std::vector<DBDataType>& bindTypes,
                                         const std::vector<std::shared_ptr<DBData>>& bindData,
                                         std::vector<std::string>& paramStorage,
                                         std::vector<const char*>& paramValues,
                                         std::vector<int>& paramLengths,
                                         std::vector<int>& paramFormats) const {
    for (size_t i = 0; i < bindTypes.size(); i++) {
        switch (bindTypes[i]) {
            case DBDataType::INTEGER: {
                auto integer = std::dynamic_pointer_cast<DBInteger>(bindData[i]);
                if (integer == nullptr) return false;
                paramStorage[i] = std::to_string(std::any_cast<int64_t>(integer->data));
                paramValues[i] = paramStorage[i].c_str();
                break;
            }
            case DBDataType::STRING: {
                auto string = std::dynamic_pointer_cast<DBString>(bindData[i]);
                if (string == nullptr) return false;
                paramStorage[i] = std::any_cast<std::string>(string->data);
                paramValues[i] = paramStorage[i].c_str();
                break;
            }
            case DBDataType::BLOB: {
                auto blob = std::dynamic_pointer_cast<DBBlob>(bindData[i]);
                if (blob == nullptr) return false;
                auto bytes = std::any_cast<std::vector<uint8_t>>(blob->data);
                paramStorage[i] = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                paramValues[i] = paramStorage[i].data();
                paramLengths[i] = static_cast<int>(paramStorage[i].size());
                paramFormats[i] = 1; // Binary format
                break;
            }
            case DBDataType::DATETIME: {
                auto datetime = std::dynamic_pointer_cast<DBDateTime>(bindData[i]);
                if (datetime == nullptr) return false;
                auto tp = std::any_cast<datetime_t>(datetime->data);
                paramStorage[i] = std::format("{:%Y-%m-%d %H:%M:%S}", tp);
                paramValues[i] = paramStorage[i].c_str();
                break;
            }
            case DBDataType::NULL_T:
                paramValues[i] = nullptr;
                break;
        }
    }

    return true;
}

bool postgresDatabase::parseResult(PGresult* result, const std::vector<DBDataType>& resultTypes,
                                   const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData) const {
    int rowCount = PQntuples(result);
    int columnCount = PQnfields(result);

    for (int row = 0; row < rowCount; row++) {
        returnedData->emplace_back();
        for (int column = 0; column < columnCount && column < resultTypes.size(); column++) {
            if (PQgetisnull(result, row, column)) {
                returnedData->back().emplace_back(new DBNull());
                continue;
            }

            const char* value = PQgetvalue(result, row, column);
            switch (resultTypes[column]) {
                case DBDataType::INTEGER: {
                    try {
                        returnedData->back().emplace_back(new DBInteger(std::stoll(value)));
                    } catch (const std::exception&) {
                        logger->log(Logger::level::FAILURE, Logger::group::DB,
                                    "Failed to parse integer: " + std::string(value));
                        return false;
                    }
                    break;
                }
                case DBDataType::STRING: {
                    returnedData->back().emplace_back(new DBString(std::string(value)));
                    break;
                }
                case DBDataType::BLOB: {
                    // bytea_output is set to hex, so values look like "\x0123ab..."
                    std::string hex(value);
                    auto blob = std::vector<uint8_t>();
                    if (hex.size() >= 2 && hex[0] == '\\' && (hex[1] == 'x' || hex[1] == 'X')) {
                        blob.reserve((hex.size() - 2) / 2);
                        for (size_t i = 2; i + 1 < hex.size(); i += 2) {
                            blob.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
                        }
                    }
                    returnedData->back().emplace_back(new DBBlob(blob));
                    break;
                }
                case DBDataType::DATETIME: {
                    std::string datetimeStr(value);
                    std::istringstream ss(datetimeStr);
                    date::sys_seconds tp;
                    date::from_stream(ss, "%Y-%m-%d %H:%M:%S", tp);
                    if (ss.fail()) {
                        logger->log(Logger::level::FAILURE, Logger::group::DB,
                                    "Failed to parse datetime: " + datetimeStr);
                        returnedData->back().emplace_back(new DBDateTime(datetime_t{}));
                    } else {
                        returnedData->back().emplace_back(new DBDateTime(tp));
                    }
                    break;
                }
                case DBDataType::NULL_T:
                    returnedData->back().emplace_back(new DBNull());
                    break;
            }
        }
    }

    return true;
}

DBResultStatus postgresDatabase::execute(const std::string& sql,
                                         const std::vector<DBDataType>& bindTypes,
                                         const std::vector<std::shared_ptr<DBData>>& bindData,
                                         const std::vector<DBDataType>& resultTypes,
                                         const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData,
                                         bool cacheStatement) {
    if (conn == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to run PostgreSQL statement: connection is null");
        return DBResultStatus::FAILURE_STMT;
    }

    auto paramCount = static_cast<int>(bindTypes.size());
    std::vector<std::string> paramStorage(paramCount);
    std::vector<const char*> paramValues(paramCount, nullptr);
    std::vector<int> paramLengths(paramCount, 0);
    std::vector<int> paramFormats(paramCount, 0);

    try {
        if (!prepareParameters(bindTypes, bindData, paramStorage, paramValues, paramLengths, paramFormats)) {
            logger->log(Logger::level::FAILURE, Logger::group::DB,
                        "Failed to bind PostgreSQL statement data (command: " + sql + ")");
            return DBResultStatus::FAILURE_DATA;
        }
    } catch (const std::bad_any_cast&) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to bind PostgreSQL statement data (command: " + sql + ")");
        return DBResultStatus::FAILURE_DATA;
    }

    logger->log(Logger::level::DEBUG, Logger::group::DB, "Running PostgreSQL statement: " + sql);

    PGresult* result;
    if (cacheStatement) {
        std::string statementName;
        auto it = statementCache.find(sql);
        if (it != statementCache.end()) {
            statementName = it->second;
        } else {
            statementName = "stmt_" + std::to_string(statementCache.size());

            PGresult* prepareResult = PQprepare(conn, statementName.c_str(), convertPlaceholders(sql).c_str(),
                                                paramCount, nullptr);
            if (PQresultStatus(prepareResult) != PGRES_COMMAND_OK) {
                logger->log(Logger::level::FAILURE, Logger::group::DB,
                            "Failed to craft PostgreSQL statement: " + std::string(PQerrorMessage(conn)) +
                            " (command: " + sql + ")");
                PQclear(prepareResult);
                return DBResultStatus::FAILURE_STMT;
            }
            PQclear(prepareResult);

            statementCache[sql] = statementName;
        }

        result = PQexecPrepared(conn, statementName.c_str(), paramCount, paramValues.data(),
                                paramLengths.data(), paramFormats.data(), 0);
    } else {
        result = PQexecParams(conn, convertPlaceholders(sql).c_str(), paramCount, nullptr, paramValues.data(),
                              paramLengths.data(), paramFormats.data(), 0);
    }

    auto status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to run PostgreSQL statement: " + std::string(PQerrorMessage(conn)) +
                    " (command: " + sql + ")");
        PQclear(result);
        return DBResultStatus::FAILURE_EXEC;
    }

    if (status == PGRES_TUPLES_OK && returnedData != nullptr && !resultTypes.empty()) {
        if (!parseResult(result, resultTypes, returnedData)) {
            PQclear(result);
            return DBResultStatus::FAILURE_EXEC;
        }
    }

    PQclear(result);
    return DBResultStatus::SUCCESS;
}

DBResultStatus postgresDatabase::executeInsertReturningId(const std::string& sql,
                                                          const std::vector<DBDataType>& bindTypes,
                                                          const std::vector<std::shared_ptr<DBData>>& bindData,
                                                          int64_t& insertedId) {
    std::string returningSql = sql;
    while (!returningSql.empty() && (std::isspace(static_cast<unsigned char>(returningSql.back())) || returningSql.back() == ';')) {
        returningSql.pop_back();
    }
    returningSql += " RETURNING id;";

    auto results = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();

    DBResultStatus status = execute(returningSql, bindTypes, bindData, {DBDataType::INTEGER}, results, true);
    if (status != DBResultStatus::SUCCESS) return status;

    if (results->empty() || results->at(0).empty()) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to obtain inserted id (command: " + sql + ")");
        return DBResultStatus::FAILURE_EXEC;
    }

    insertedId = std::any_cast<int64_t>(results->at(0).at(0)->data);
    if (insertedId < 0) {
        return DBResultStatus::FAILURE_EXEC;
    }

    return DBResultStatus::SUCCESS;
}

bool postgresDatabase::inTransaction() const {
    return conn != nullptr && PQtransactionStatus(conn) != PQTRANS_IDLE;
}

std::string postgresDatabase::beginTransactionSQL(bool immediate) const {
    // PostgreSQL has no equivalent of SQLite's BEGIN IMMEDIATE; a plain
    // transaction takes locks as statements execute, which is good enough here.
    return "BEGIN TRANSACTION;";
}

std::string postgresDatabase::greatestFunction() const {
    return "GREATEST";
}

std::string postgresDatabase::caseInsensitiveLikeOperator() const {
    // SQLite's LIKE is case-insensitive by default, so use ILIKE for parity
    return "ILIKE";
}

std::string postgresDatabase::backendName() const {
    return "PostgreSQL";
}

void postgresDatabase::close() {
    if (conn == nullptr) return;

    shutdownQueueThread();

    // Server-side prepared statements are deallocated with the connection
    statementCache.clear();

    PQfinish(conn);
    conn = nullptr;
}

} // namespace db
