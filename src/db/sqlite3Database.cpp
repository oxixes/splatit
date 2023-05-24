#include <memory>
#include <utility>
#include "sqlite3Database.hpp"

// TODO Switch to smart pointers

namespace db {

sqlite3Database::sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath) : Database(std::move(logger)) {
    this->dbPath = dbPath;
}

sqlite3Database::~sqlite3Database() {
    close();
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

bool sqlite3Database::run() {
    running = true;
    dbThreadHandle = std::thread(&sqlite3Database::dbThread, this);
    logger->log(Logger::level::INFO, Logger::group::DB, "SQLite 3 database thread started.");
    return true;
}

int sqlite3Database::queueCommand(Command* command, bool commandMutex) {
    //std::unique_lock dbLock(dbThreadMutex);
    std::unique_lock lock(commandQueueMutex);
    command->commandId = commandId++;
    commandQueue.push(command);

    if (commandMutex) {
        auto* mutex = new std::mutex();
        auto* cv = new std::condition_variable();
        auto condition_variable_info = std::make_tuple(command->commandId, mutex, cv, std::this_thread::get_id());
        std::unique_lock cmdMutexLock(commandCVsMutex);
        commandCVs.push_back(std::move(condition_variable_info));
        command->hasMutex = true;
    }

    return command->commandId;
}

void sqlite3Database::processQueue() {
    dbThreadCV.notify_one();
}

void sqlite3Database::waitForCommand(int commandId, bool* shouldEnd) {
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

    std::mutex* mutex = std::get<1>(*CVInfo);
    std::condition_variable* cv = std::get<2>(*CVInfo);
    commandCVsLock.unlock();

    std::unique_lock lock(*mutex);
    (*cv).wait(lock, [this, commandId, shouldEnd] {
        if (!running || *shouldEnd) {
            return true;
        } else {
            std::unique_lock resultsLock(resultsMutex);
            return std::ranges::any_of(results, [commandId](Result* result) {
                return result->commandId == commandId;
            });
        }
    });
}

void sqlite3Database::waitForQueue(bool* shouldEnd) {
    std::unique_lock lock(dbThreadMutex);
    dbThreadCV.wait(lock, [this, shouldEnd] {
        if (!running || *shouldEnd) return true;
        std::unique_lock lock(commandQueueMutex);
        return commandQueue.empty();
    });
}

void sqlite3Database::clearCommandMutex(int commandId) {
    std::unique_lock lock(commandCVsMutex);
    std::erase_if(commandCVs, [commandId](const auto& info) {
        if (std::get<0>(info) == commandId && std::this_thread::get_id() != std::get<3>(info)) {
            throw std::runtime_error("Attempted to clear command mutex from a different thread where it was created.");
        }

        return std::get<0>(info) == commandId;
    });
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

            Command* command = commandQueue.front();
            processCommand(command);
            commandQueue.pop();

            if (command->hasMutex) {
                int commandId = command->commandId;
                std::unique_lock commandCVsLock(commandCVsMutex);
                auto CVInfo = std::find_if(commandCVs.begin(), commandCVs.end(), [commandId] (const auto& info) {
                    return std::get<0>(info) == commandId;
                });

                if (CVInfo == commandCVs.end()) {
                    logger->log(Logger::level::DEBUG, Logger::group::DB,
                                "Attempted to notify a command that does not exist or doesn't have a mutex (ID: " +
                                std::to_string(commandId) + ")");
                } else {
                    std::condition_variable* cv = std::get<2>(*CVInfo);
                    cv->notify_all();
                }
            }

            delete command;
        }

        queueLock.unlock();
        lock.unlock();
        dbThreadCV.notify_all();
        if (!running) break;
    }
}

void sqlite3Database::processCommand(Command* command) {
    auto* returnedData = new std::vector<std::vector<DBData*>*>();
    sqlite3_stmt* statement = nullptr;

    std::vector<std::any> resultsData;
    resultStatus resultStatus = resultStatus::SUCCESS;

    switch (command->type) {
        case commandType::GENERIC:
            if (command->data.size() != 4 || command->data[0].type() != typeid(std::string) ||
                command->data[1].type() != typeid(std::vector<dbDataType>) ||
                command->data[2].type() != typeid(std::vector<DBData*>) ||
                command->data[3].type() != typeid(std::vector<dbDataType>)) {
                logger->log(Logger::level::ERROR, Logger::group::DB,
                            "Failed to run SQLite 3 statement: invalid command data");
                break;
            }

            // TODO Improve statuses
            if (!craftStatement(std::any_cast<std::string>(command->data[0]), &statement)) {
                resultStatus = resultStatus::FAILURE_GENERIC;
                break;
            }

            if (!bindData(statement, std::any_cast<std::vector<dbDataType>>(command->data[3]),
                          std::any_cast<std::vector<DBData*>>(command->data[2]))) {
                resultStatus = resultStatus::FAILURE_GENERIC;
                break;
            }

            if (!runStatement(statement, std::any_cast<std::vector<dbDataType>>(command->data[3]), returnedData)) {
                resultStatus = resultStatus::FAILURE_GENERIC;
                break;
            }

            sqlite3_finalize(statement);

            // TODO This should be moved elsewhere
            for (auto* row : *returnedData) {
                std::vector<std::any> rowData;
                for (auto* data : *row) {
                    switch (data->type) {
                        case dbDataType::INTEGER:
                            rowData.emplace_back(dynamic_cast<DBInteger*>(data));
                            break;
                        case dbDataType::STRING:
                            rowData.emplace_back(dynamic_cast<DBString*>(data));
                            break;
                    }
                }

                resultsData.emplace_back(std::move(rowData));
            }
            break;
    }

    freeData(returnedData);

    std::unique_lock lock(resultsMutex);
    results.push_back(new Result(command->commandId, resultStatus, resultsData));
}

bool sqlite3Database::craftStatement(const std::string& command, sqlite3_stmt** outStatement) {
    if (sqlite3_prepare_v2(db, command.c_str(), -1, outStatement, nullptr) != SQLITE_OK) {
        logger->log(Logger::level::ERROR, Logger::group::DB,
                    "Failed to craft SQLite 3 statement: " + std::string(sqlite3_errmsg(db)) +
                    " (command: " + command + ")");
        return false;
    }

    return true;
}

bool sqlite3Database::bindData(sqlite3_stmt *statement, const std::vector<dbDataType>& dataTypes,
                               const std::vector<DBData*>& data) {
    for (int i = 0; i < dataTypes.size(); i++) {
        int result = 0;
        switch (dataTypes[i]) {
            case dbDataType::INTEGER:
                result = sqlite3_bind_int(statement, i + 1, std::any_cast<int>(dynamic_cast<DBInteger*>(data[i])->data));
                break;
            case dbDataType::STRING:
                result = sqlite3_bind_text(statement, i + 1, std::any_cast<std::string>(dynamic_cast<DBString*>(data[i])->data).c_str(), -1,
                                           SQLITE_TRANSIENT);
                break;
        }

        if (result != SQLITE_OK) {
            logger->log(Logger::level::ERROR, Logger::group::DB,
                        "Failed to bind SQLite 3 statement data: " + std::string(sqlite3_errmsg(db)));
            return false;
        }
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
    if (db == nullptr) return;

    running = false;
    dbThreadCV.notify_all();
    dbThreadHandle.join();

    logger->log(Logger::level::INFO, Logger::group::DB, "SQLite 3 database thread stopped.");

    // NOTE: All threads should be stopped at this point, but just in case,
    //       we'll notify all the condition variables so that any thread that
    //       is waiting for a command to finish will be notified and can exit
    std::unique_lock CVLock(commandCVsMutex);
    for (auto cv : commandCVs) {
        std::get<2>(cv)->notify_all();
    }

    std::unique_lock queueLock(commandQueueMutex);
    while (!commandQueue.empty()) {
        Command* command = commandQueue.front();
        commandQueue.pop();
        delete command;
    }
    queueLock.unlock();

    std::unique_lock resultsLock(resultsMutex);
    for (auto* result : results) delete result;
    results.clear();
    resultsLock.unlock();

    // NOTE: Maybe we should let all threads that created the mutexes destroy them?
    for (auto CVInfo : commandCVs) {
        delete std::get<1>(CVInfo);
        delete std::get<2>(CVInfo);
    }
    commandCVs.clear();
    CVLock.unlock();

    sqlite3_close(db);
    db = nullptr;
}

} // namespace db