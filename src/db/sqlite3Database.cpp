#include <memory>
#include <utility>
#include "sqlite3Database.hpp"

namespace db {

sqlite3Database::sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath) :
        Database(std::move(logger), DBType::SQLITE3, DBVersion::EMPTY) {
    this->dbPath = dbPath;
}

sqlite3Database::~sqlite3Database() {
    close();
}

bool sqlite3Database::init() {
    if (!util::checkParentDirectory(dbPath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to open SQLite 3 database: parent directory does not exist");
        return false;
    }

    bool dbExists = fs::exists(dbPath);

    if (sqlite3_open_v2(dbPath.string().c_str(), &db,
                        SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to open SQLite 3 database: " + std::string(sqlite3_errmsg(db)));
        return false;
    }

    if (dbExists) {
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

    return true;
}

DBVersion sqlite3Database::obtainVersion() {
    std::string sql = "SELECT version FROM db_info LIMIT 1;";

    sqlite3_stmt* stmt;
    if (!craftStatement(sql, &stmt)) {
        throw std::runtime_error("Failed to craft statement");
    }

    auto results = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();
    auto dataTypes = std::vector<dbDataType>{dbDataType::STRING};

    if (!runStatement(stmt, dataTypes, results)) {
        throw std::runtime_error("Failed to run statement");
    }

    if (results->empty() || results->at(0).empty()) {
        throw std::runtime_error("No version data found");
    }

    try {
        return migrations::getVersionFromString(std::any_cast<std::string>(
                std::dynamic_pointer_cast<DBString>(results->at(0).at(0))->data));
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Failed to parse version data: " + std::string(e.what()));
    }
}

bool sqlite3Database::run() {
    running = true;
    dbThreadHandle = std::thread(&sqlite3Database::dbThread, this);
    logger->log(Logger::level::INFO, Logger::group::DB, "SQLite 3 database thread started.");
    return true;
}

uint32_t sqlite3Database::queueCommand(std::unique_ptr<Command> command, bool commandMutex) {
    //std::unique_lock dbLock(dbThreadMutex);
    std::unique_lock lock(commandQueueMutex);
    command->commandId = commandId;

    if (commandMutex) {
        auto mutex = std::make_unique<std::mutex>();
        auto cv = std::make_unique<std::condition_variable>();
        auto condition_variable_info = std::make_tuple(command->commandId, std::move(mutex),
                                                       std::move(cv), std::this_thread::get_id());
        std::unique_lock cmdMutexLock(commandCVsMutex);
        commandCVs.push_back(std::move(condition_variable_info));
        command->hasMutex = true;
    }

    commandQueue.push(std::move(command));

    return commandId++;
}

void sqlite3Database::processQueue() {
    dbThreadCV.notify_one();
}

void sqlite3Database::waitForCommand(uint32_t commandId, std::shared_ptr<bool> shouldEnd) {
    std::unique_lock commandCVsLock(commandCVsMutex);
    auto CVInfo = std::find_if(commandCVs.begin(), commandCVs.end(), [commandId] (const auto& info) {
        return std::get<0>(info) == commandId;
    });

    if (CVInfo == commandCVs.end()) {
        logger->log(Logger::level::DEBUG, Logger::group::DB,
                    "Attempted to wait for a command that does not exist or doesn't have a mutex (ID: " +
                    std::to_string(commandId) + ")");
        return;
    }

    // FIXME: Use smart pointers here?
    std::mutex* mutex = std::get<1>(*CVInfo).get();
    std::condition_variable* cv = std::get<2>(*CVInfo).get();
    commandCVsLock.unlock();

    std::unique_lock lock(*mutex);
    (*cv).wait(lock, [this, commandId, shouldEnd] {
        if (!running || (shouldEnd != nullptr && *shouldEnd)) {
            return true;
        } else {
            std::unique_lock resultsLock(resultsMutex);
            return std::ranges::any_of(results, [commandId](const std::unique_ptr<Result>& result) {
                return result->commandId == commandId;
            });
        }
    });
}

void sqlite3Database::waitForQueue(std::shared_ptr<bool> shouldEnd) {
    std::unique_lock lock(dbThreadMutex);
    dbThreadCV.wait(lock, [this, shouldEnd] {
        if (!running || (shouldEnd != nullptr && *shouldEnd)) return true;
        std::unique_lock lock(commandQueueMutex);
        return commandQueue.empty();
    });
}

void sqlite3Database::clearCommandMutex(uint32_t commandId) {
    std::unique_lock lock(commandCVsMutex);
    std::erase_if(commandCVs, [commandId](const auto& info) {
        if (std::get<0>(info) == commandId && std::this_thread::get_id() != std::get<3>(info)) {
            throw std::runtime_error("Attempted to clear command mutex from a different thread where it was created.");
        }

        return std::get<0>(info) == commandId;
    });
}

void sqlite3Database::notifyCommand(uint32_t commandId) {
    std::unique_lock lock(commandCVsMutex);
    auto CVInfo = std::find_if(commandCVs.begin(), commandCVs.end(), [commandId] (const auto& info) {
        return std::get<0>(info) == commandId;
    });

    if (CVInfo == commandCVs.end()) {
        logger->log(Logger::level::DEBUG, Logger::group::DB,
                    "Attempted to notify a command that does not exist or doesn't have a mutex (ID: " +
                    std::to_string(commandId) + ")");
        return;
    }

    std::condition_variable* cv = std::get<2>(*CVInfo).get();
    lock.unlock();
    (*cv).notify_all();
}

void sqlite3Database::notifyQueue() {
    dbThreadCV.notify_all();
}

void sqlite3Database::dbThread() {
    while (true) {
        std::unique_lock lock(dbThreadMutex);
        dbThreadCV.wait(lock, [this] {
            if (!running) return true;
            std::unique_lock lock(commandQueueMutex);
            return !commandQueue.empty();
        });

        if (!running) {
            lock.unlock();
            dbThreadCV.notify_all();
            break;
        }

        std::unique_lock queueLock(commandQueueMutex);
        while (!commandQueue.empty()) {
            if (!running) break;

            std::unique_ptr<Command> command = std::move(commandQueue.front());
            processCommand(command);
            commandQueue.pop();

            if (command->hasMutex) {
                uint32_t commandId = command->commandId;
                std::unique_lock commandCVsLock(commandCVsMutex);
                auto CVInfo = std::find_if(commandCVs.begin(), commandCVs.end(), [commandId] (const auto& info) {
                    return std::get<0>(info) == commandId;
                });

                if (CVInfo == commandCVs.end()) {
                    logger->log(Logger::level::DEBUG, Logger::group::DB,
                                "Attempted to notify a command that does not exist or doesn't have a mutex (ID: " +
                                std::to_string(commandId) + ")");
                } else {
                    std::get<2>(*CVInfo)->notify_all();
                }
            }
        }

        queueLock.unlock();
        lock.unlock();
        dbThreadCV.notify_all();
        if (!running) break;
    }
}

void sqlite3Database::processCommand(const std::unique_ptr<Command>& command) {
    auto returnedData = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();
    sqlite3_stmt* statement = nullptr;

    std::vector<std::any> resultsData;
    DBResultStatus resultStatus = DBResultStatus::SUCCESS;

    if (!verifyCommandArgs(command)) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to run SQLite 3 statement: invalid command arguments");
        resultStatus = DBResultStatus::FAILURE_ARGS;

        goto push_results;
    }

    if (command->type == DBCommandType::GENERIC) {
        if (!craftStatement(std::any_cast<std::string>(command->data[0]), &statement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(statement, std::any_cast<std::vector<dbDataType>>(command->data[3]),
                      std::any_cast<std::vector<std::shared_ptr<DBData>>>(command->data[2]))) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(statement);
            goto push_results;
        }

        if (!runStatement(statement, std::any_cast<std::vector<dbDataType>>(command->data[3]), returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(statement);
            goto push_results;
        }

        sqlite3_finalize(statement);

        // TODO This should be moved elsewhere
        for (const auto& row : *returnedData) {
            std::vector<std::any> rowData;
            for (const auto& data : row) {
                switch (data->type) {
                    case dbDataType::INTEGER:
                        rowData.emplace_back(std::dynamic_pointer_cast<DBInteger>(data));
                        break;
                    case dbDataType::STRING:
                        rowData.emplace_back(std::dynamic_pointer_cast<DBString>(data));
                        break;
                }
            }

            resultsData.emplace_back(std::move(rowData));
        }
    } else if (command->type == DBCommandType::GET_USER_BY_PID || command->type == DBCommandType::GET_USER_BY_USERNAME) {
        // Since the statement is always the same, we can just prepare it once
        if (command->type == DBCommandType::GET_USER_BY_PID && getUserByPIDStatement == nullptr) {
            if (!craftStatement("SELECT * FROM users WHERE pid = ?;", &getUserByPIDStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        } else if (command->type == DBCommandType::GET_USER_BY_USERNAME && getUserByUsernameStatement == nullptr) {
            if (!craftStatement("SELECT * FROM users WHERE username = ?;", &getUserByUsernameStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        statement = (command->type == DBCommandType::GET_USER_BY_PID) ? getUserByPIDStatement : getUserByUsernameStatement;

        std::shared_ptr<DBData> identifier;
        dbDataType identifierType;
        if (command->type == DBCommandType::GET_USER_BY_PID) {
            identifierType = dbDataType::INTEGER;
            identifier = std::make_shared<DBInteger>((int64_t) std::any_cast<uint32_t>(command->data[0]));
        } else {
            identifierType = dbDataType::STRING;
            identifier = std::make_shared<DBString>(std::any_cast<std::string>(command->data[0]));
        }

        if (!bindData(statement, {identifierType}, {identifier})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(statement);
            goto push_results;
        }

        std::vector<dbDataType> returnedDataTypes {dbDataType::INTEGER, dbDataType::STRING, dbDataType::STRING};

        if (!runStatement(statement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            goto push_results;
        }

        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);

        // There will be none or one row returned, so just add the data to the results vector
        for (const auto& row : *returnedData) {
            for (const auto& data : row) {
                switch (data->type) {
                    case dbDataType::INTEGER:
                        resultsData.emplace_back((uint32_t) std::any_cast<int64_t>(data->data));
                        break;
                    case dbDataType::STRING:
                        resultsData.emplace_back(std::any_cast<std::string>(data->data));
                        break;
                }
            }
        }
    } else if (command->type == DBCommandType::GET_GAME_SERVER_ACCESS) {
        if (getGameServerAccessStatement == nullptr) {
            if (!craftStatement("SELECT * FROM game_server_access WHERE pid = ? AND game_server_id = ?;",
                                &getGameServerAccessStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(getGameServerAccessStatement, {dbDataType::INTEGER, dbDataType::STRING},
                      {std::make_shared<DBInteger>((int64_t) std::any_cast<uint32_t>(command->data[0])),
                       std::make_shared<DBString>(std::any_cast<std::string>(command->data[1]))})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getGameServerAccessStatement);
            goto push_results;
        }

        std::vector<dbDataType> returnedDataTypes {dbDataType::INTEGER, dbDataType::STRING, dbDataType::STRING};

        if (!runStatement(getGameServerAccessStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getGameServerAccessStatement);
            sqlite3_clear_bindings(getGameServerAccessStatement);
            goto push_results;
        }

        sqlite3_reset(getGameServerAccessStatement);
        sqlite3_clear_bindings(getGameServerAccessStatement);

        if (!returnedData->empty()) resultsData.emplace_back(std::any_cast<std::string>((*returnedData)[0][2]->data));
    }

    push_results:
    std::unique_lock lock(resultsMutex);
    results.push_back(std::make_unique<Result>(command->commandId, resultStatus, resultsData));
}

bool sqlite3Database::craftStatement(const std::string& command, sqlite3_stmt** outStatement) {
    if (sqlite3_prepare_v2(db, command.c_str(), -1, outStatement, nullptr) != SQLITE_OK) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to craft SQLite 3 statement: " + std::string(sqlite3_errmsg(db)) +
                    " (command: " + command + ")");
        return false;
    }

    return true;
}

bool sqlite3Database::bindData(sqlite3_stmt *statement, const std::vector<dbDataType>& dataTypes,
                               const std::vector<std::shared_ptr<DBData>>& data) {
    for (int i = 0; i < dataTypes.size(); i++) {
        int result = 0;
        switch (dataTypes[i]) {
            case dbDataType::INTEGER:
                result = sqlite3_bind_int64(statement, i + 1, std::any_cast<int64_t>(
                        std::dynamic_pointer_cast<DBInteger>(data[i])->data));
                break;
            case dbDataType::STRING:
                result = sqlite3_bind_text(statement, i + 1, std::any_cast<std::string>(
                        std::dynamic_pointer_cast<DBString>(data[i])->data).c_str(), -1, SQLITE_TRANSIENT);
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

bool sqlite3Database::runStatement(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                                   const std::unique_ptr<std::vector<std::vector<std::shared_ptr<DBData>>>>& returnedData) {
    if (statement == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::DB, "Failed to run SQLite 3 statement: statement is null");
        return false;
    }

    logger->log(Logger::level::DEBUG, Logger::group::DB, "Running SQLite 3 statement: " +
                                                         std::string(sqlite3_expanded_sql(statement)));

    int result = sqlite3_step(statement);
    //returnedData = new std::vector<std::vector<DBData*>*>();
    while (result != SQLITE_DONE) {
        if (result == SQLITE_ROW && returnedData != nullptr) {
            //auto* row = new std::vector<DBData*>();
            returnedData->emplace_back();
            for (int i = 0; i < sqlite3_column_count(statement); i++) {
                switch (dataTypes[i]) {
                    case dbDataType::INTEGER:
                        returnedData->back().emplace_back(new DBInteger(sqlite3_column_int64(statement, i)));
                        break;
                    case dbDataType::STRING:
                        returnedData->back().emplace_back(new DBString(std::string(
                                reinterpret_cast<const char *>(sqlite3_column_text(statement, i)))));
                        break;
                }
            }
        } else {
            logger->log(Logger::level::FAILURE, Logger::group::DB,
                        "Failed to run SQLite 3 statement: " + std::string(sqlite3_errmsg(db)));
            //freeData(returnedData);
            return false;
        }

        result = sqlite3_step(statement);
    }

    return true;
}

void sqlite3Database::close() {
    if (db == nullptr) return;

    running = false;
    dbThreadCV.notify_all();
    dbThreadHandle.join();

    logger->log(Logger::level::INFO, Logger::group::DB, "SQLite 3 database thread stopped.");

    // NOTE: All threads should be stopped at this point, but just in case,
    //       we'll notify all the condition variables so that any thread that
    //       is waiting for a command to finish will be notified and can exit
    std::unique_lock CVLock(commandCVsMutex);
    for (auto& cv : commandCVs) {
        std::get<2>(cv)->notify_all();
    }

    std::unique_lock queueLock(commandQueueMutex);
    while (!commandQueue.empty()) {
        std::unique_ptr<Command> command = std::move(commandQueue.front());
        commandQueue.pop();
    }
    queueLock.unlock();

    std::unique_lock resultsLock(resultsMutex);
    results.clear();
    resultsLock.unlock();

    // NOTE: Maybe we should let all threads that created the mutexes destroy them?
    commandCVs.clear();
    CVLock.unlock();

    sqlite3_close(db);
    db = nullptr;
}

} // namespace db