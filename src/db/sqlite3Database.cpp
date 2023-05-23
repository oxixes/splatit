#include <memory>
#include "sqlite3Database.hpp"

sqlite3Database::sqlite3Database(Logger::Logger* logger, const fs::path& dbPath) : Database(logger) {
    this->dbPath = dbPath;
}

bool sqlite3Database::init() {
    if (!util::checkParentDirectory(dbPath)) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    "Failed to open SQLite 3 database: parent directory does not exist");
        return false;
    }

    if (sqlite3_open_v2(dbPath.string().c_str(), &db,
                        SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    "Failed to open SQLite 3 database: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    return true;
}

bool sqlite3Database::craftCommand(const std::string& command, sqlite3_stmt** outStatement) {
    if (sqlite3_prepare_v2(db, command.c_str(), -1, outStatement, nullptr) != SQLITE_OK) {
        logger->log(Logger::level::ERROR, Logger::group::DB,
                    "Failed to craft SQLite 3 statement: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    return true;
}

bool sqlite3Database::runStatement(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                                   std::vector<std::vector<DBData*>*>* returnedData) {
    if (statement == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::DB, "Failed to run SQLite 3 statement: statement is null");
        return false;
    }

    logger->log(Logger::level::DEBUG, Logger::group::DB, "Running SQLite 3 statement: " +
                                                         std::string(sqlite3_expanded_sql(statement)));

    int result = sqlite3_step(statement);
    //returnedData = new std::vector<std::vector<DBData*>*>();
    while (result != SQLITE_DONE) {
        if (result == SQLITE_ROW && returnedData != nullptr) {
            auto* row = new std::vector<DBData*>();
            for (int i = 0; i < sqlite3_column_count(statement); i++) {
                switch (dataTypes[i]) {
                    case dbDataType::INTEGER:
                        row->push_back((DBData*) new DBInteger(sqlite3_column_int(statement, i)));
                        break;
                    case dbDataType::STRING:
                        row->push_back((DBData*) new DBString(std::string(
                                reinterpret_cast<const char *>(sqlite3_column_text(statement, i)))));
                        break;
                }
            }

            returnedData->push_back(row);
        } else {
            logger->log(Logger::level::ERROR, Logger::group::DB,
                        "Failed to run SQLite 3 statement: " + std::string(sqlite3_errmsg(db)));
            //freeData(returnedData);
            return false;
        }

        result = sqlite3_step(statement);
    }

    return true;
}

void sqlite3Database::freeData(std::vector<std::vector<DBData*>*>* data) {
    if (data == nullptr) return;

    for (auto& row : *data) {
        if (row == nullptr) continue;

        for (auto& column : *row) {
            if (column == nullptr) continue;
            delete column;
        }

        delete row;
    }

    delete data;
}

void sqlite3Database::close() {

}