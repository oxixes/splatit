#include <memory>
#include <utility>
#include <chrono>
#include <date/date.h>

#include "sqlite3Database.hpp"

namespace db {

sqlite3Database::sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath) :
        Database(std::move(logger), DBType::SQLITE3, DBVersion::EMPTY) {
    this->dbPath = dbPath;
    dbQueueCV = std::make_shared<std::condition_variable>();
}

sqlite3Database::sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath,
        std::shared_ptr<std::queue<std::unique_ptr<Command>>> commandQueue,
        std::shared_ptr<std::mutex> commandQueueMutex, std::shared_ptr<std::atomic<bool>> shouldStop,
        std::shared_ptr<std::thread> dbThreadHandle, std::shared_ptr<std::condition_variable> dbQueueCV,
        std::shared_ptr<std::mutex> queueWaitMutex, std::shared_ptr<std::condition_variable> dbQueueWaitCV,
        DBType dbType, DBVersion dbVersion) : Database(std::move(logger), dbType, dbVersion) {
    this->commandQueue = std::move(commandQueue);
    this->commandQueueMutex = std::move(commandQueueMutex);
    this->shouldStop = std::move(shouldStop);
    this->dbThreadHandle = std::move(dbThreadHandle);
    this->dbQueueCV = std::move(dbQueueCV);
    this->queueWaitMutex = std::move(queueWaitMutex);
    this->dbQueueWaitCV = std::move(dbQueueWaitCV);
    this->dbPath = dbPath;

    isSession = true; // This is a session database, not the main one
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

    return true;
}

DBVersion sqlite3Database::obtainVersion() {
    std::string sql = "SELECT version FROM db_info LIMIT 1;";

    sqlite3_stmt* stmt;
    if (!craftStatement(sql, &stmt)) {
        throw std::runtime_error("Failed to craft statement");
    }

    auto results = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();
    auto dataTypes = std::vector<DBDataType>{DBDataType::STRING};

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
    *shouldStop = false;
    dbThreadHandle = std::make_shared<std::thread>(&sqlite3Database::dbThread, this);
    logger->log(Logger::level::INFO, Logger::group::DB, "SQLite 3 database thread started.");
    return true;
}

async::ManualTask<Result> sqlite3Database::queueCommand(std::shared_ptr<async::Scheduler> scheduler, std::unique_ptr<Command> command) {
    if (*shouldStop) return async::ManualTask<Result>(nullptr);

    auto task = std::make_shared<async::ManualTask<Result>>(scheduler);

    command->task = task;
    command->db = this->shared_from_this();

    std::unique_lock lock(*commandQueueMutex);
    commandQueue->emplace(std::move(command));

    return *task;
}

async::ManualTask<Result> sqlite3Database::startTransaction(std::shared_ptr<async::Scheduler> scheduler) {
    auto command = craftVoidCommand("BEGIN TRANSACTION;");
    auto task = std::move(queueCommand(std::move(scheduler), std::move(command)));

    processQueue();

    return std::move(task);
}

async::ManualTask<Result> sqlite3Database::commitTransaction(std::shared_ptr<async::Scheduler> scheduler) {
    auto command = craftVoidCommand("COMMIT TRANSACTION;");
    auto task = std::move(queueCommand(std::move(scheduler), std::move(command)));

    processQueue();

    return std::move(task);
}

async::ManualTask<Result> sqlite3Database::rollbackTransaction(std::shared_ptr<async::Scheduler> scheduler) {
    auto command = craftVoidCommand("ROLLBACK TRANSACTION;");
    auto task = std::move(queueCommand(std::move(scheduler), std::move(command)));

    processQueue();

    return std::move(task);
}

void sqlite3Database::processQueue() {
    dbQueueCV->notify_one();
}

void sqlite3Database::waitForQueue() {
    std::unique_lock lock(*queueWaitMutex);
    dbQueueWaitCV->wait(lock, [this] {
        return commandQueue->empty() || *shouldStop;
    });
}

void sqlite3Database::dbThread() const {
    while (true) {
        std::unique_lock lock(*commandQueueMutex);
        dbQueueCV->wait(lock, [this] {
            if (*shouldStop) return true;
            return !commandQueue->empty();
        });

        if (*shouldStop) break;

        while (!commandQueue->empty() && !*shouldStop) {
            std::unique_ptr<Command> command = std::move(commandQueue->front());
            commandQueue->pop();

            lock.unlock();
            std::static_pointer_cast<sqlite3Database>(command->db)->processCommand(command);
            lock.lock();
        }

        dbQueueWaitCV->notify_all();
        if (*shouldStop) break;
    }

    dbQueueWaitCV->notify_all();
}

void sqlite3Database::processCommand(const std::unique_ptr<Command>& command)
{
    auto returnedData = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();
    sqlite3_stmt* statement = nullptr;

    std::any resultsData;
    DBResultStatus resultStatus = DBResultStatus::SUCCESS;

    if (!verifyCommandArgs(command)) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to run SQLite 3 statement: invalid command arguments");
        resultStatus = DBResultStatus::FAILURE_ARGS;

        goto push_results;
    }

    if (command->type == DBCommandType::GENERIC) {
        auto* query = std::any_cast<DBGenericCommand>(&command->data);

        if (!craftStatement(query->cmd, &statement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(statement, query->bindTypes, query->bindData)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(statement);
            goto push_results;
        }

        if (!runStatement(statement, query->resultTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(statement);
            goto push_results;
        }

        sqlite3_finalize(statement);

        DBGenericResult result {
            .data = std::move(*returnedData)
        };

        resultsData = std::move(result);
    } else if (command->type == DBCommandType::GET_USER_BY_PID || command->type == DBCommandType::GET_USER_BY_USERNAME) {
        // Since the statement is always the same, we can just prepare it once
        if (command->type == DBCommandType::GET_USER_BY_PID && getUserByPIDStatement == nullptr) {
            if (!craftStatement("SELECT pid, username, password FROM users WHERE pid = ?;", &getUserByPIDStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        } else if (command->type == DBCommandType::GET_USER_BY_USERNAME && getUserByUsernameStatement == nullptr) {
            if (!craftStatement("SELECT pid, username, password FROM users WHERE username = ?;", &getUserByUsernameStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        statement = (command->type == DBCommandType::GET_USER_BY_PID) ? getUserByPIDStatement : getUserByUsernameStatement;

        std::shared_ptr<DBData> identifier;
        DBDataType identifierType;
        if (command->type == DBCommandType::GET_USER_BY_PID) {
            auto* query = std::any_cast<DBPidQuery>(&command->data);
            identifierType = DBDataType::INTEGER;
            identifier = std::make_shared<DBInteger>((int64_t) query->pid);
        } else {
            auto* query = std::any_cast<DBUsernameQuery>(&command->data);
            identifierType = DBDataType::STRING;
            identifier = std::make_shared<DBString>(query->username);
        }

        if (!bindData(statement, {identifierType}, {identifier})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(statement);
            goto push_results;
        }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING};

        if (!runStatement(statement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            goto push_results;
        }

        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);

        if (!returnedData->empty()) {
            DBUserData userData {
                .pid = (uint32_t) std::any_cast<int64_t>((*returnedData)[0][0]->data),
                .username = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .password = std::any_cast<std::string>((*returnedData)[0][2]->data)
            };
            resultsData = std::move(userData);
        }
    } else if (command->type == DBCommandType::GET_GAME_SERVER_ACCESS) {
        if (getGameServerAccessStatement == nullptr) {
            if (!craftStatement("SELECT * FROM game_server_access WHERE pid = ?;",
                                &getGameServerAccessStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
                                }
        }

        auto* query = std::any_cast<DBGameServerAccessQuery>(&command->data);

        if (!bindData(getGameServerAccessStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>((int64_t) query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getGameServerAccessStatement);
            goto push_results;
                      }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING};

        if (!runStatement(getGameServerAccessStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getGameServerAccessStatement);
            sqlite3_clear_bindings(getGameServerAccessStatement);
            goto push_results;
        }

        sqlite3_reset(getGameServerAccessStatement);
        sqlite3_clear_bindings(getGameServerAccessStatement);

        if (!returnedData->empty()) {
            DBGameServerAccessData accessData {
                .pid = (uint32_t) std::any_cast<int64_t>((*returnedData)[0][0]->data),
                .password = std::any_cast<std::string>((*returnedData)[0][1]->data)
            };

            resultsData = std::move(accessData);
        }
    } else if (command->type == DBCommandType::INSERT_GAME_SERVER_ACCESS) {
        if (insertGameServerAccessStatement == nullptr) {
            if (!craftStatement("INSERT INTO game_server_access (pid, password) VALUES (?, ?);",
                                &insertGameServerAccessStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
                                }
        }

        auto* query = std::any_cast<DBGameServerAccessData>(&command->data);

        if (!bindData(insertGameServerAccessStatement, {DBDataType::INTEGER, DBDataType::STRING},
                      {std::make_shared<DBInteger>((int64_t) query->pid), std::make_shared<DBString>(query->password)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertGameServerAccessStatement);
            goto push_results;
                      }

        if (!runStatement(insertGameServerAccessStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertGameServerAccessStatement);
            sqlite3_clear_bindings(insertGameServerAccessStatement);
            goto push_results;
        }

        sqlite3_reset(insertGameServerAccessStatement);
        sqlite3_clear_bindings(insertGameServerAccessStatement);
    } else if (command->type == DBCommandType::GET_USER_INFO) {
        if (getUserInfoStatement == nullptr) {
            if (!craftStatement("SELECT * FROM user_info WHERE pid = ?;", &getUserInfoStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getUserInfoStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>((int64_t) query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
                      }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::BLOB, DBDataType::BLOB,
                                                   DBDataType::BLOB, DBDataType::DATETIME};

        if (!runStatement(getUserInfoStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getUserInfoStatement);
            sqlite3_clear_bindings(getUserInfoStatement);
            goto push_results;
        }

        sqlite3_reset(getUserInfoStatement);
        sqlite3_clear_bindings(getUserInfoStatement);

        if (!returnedData->empty()) {
            DBUserInfoData userInfoData {
                .pid = (uint32_t) std::any_cast<int64_t>((*returnedData)[0][0]->data),
                .showPresence = (bool) std::any_cast<int64_t>((*returnedData)[0][1]->data),
                .showPlaying = (bool) std::any_cast<int64_t>((*returnedData)[0][2]->data),
                .blockRequests = (bool) std::any_cast<int64_t>((*returnedData)[0][3]->data),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][4]->data),
                .presence = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][5]->data),
                .comment = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][6]->data),
                .lastOnline = std::any_cast<datetime_t>((*returnedData)[0][7]->data)
            };

            resultsData = std::move(userInfoData);
        }
    } else if (command->type == DBCommandType::GET_FRIENDS_INFO) {
        if (getFriendsInfoStatement == nullptr) {
            std::string sqlCommand =
                    "SELECT fuser.pid AS friend_pid, "
                    "fuser.show_presence AS show_presence, "
                    "fuser.show_playing AS show_game, "
                    "fuser.block_requests AS block_requests, "
                    "fuser.nna_info AS nna_info, "
                    "fuser.presence AS presence, "
                    "fuser.comment AS comment, "
                    "fuser.last_online AS last_online, "
                    "friendships.became_friends AS became_friends "
                    "FROM user_info "
                    "JOIN friendships "
                    "ON user_info.pid = friendships.pid "
                    "OR user_info.pid = friendships.friend_pid "
                    "JOIN user_info AS fuser "
                    "ON (fuser.pid = friendships.pid AND fuser.pid <> user_info.pid) "
                    "OR (fuser.pid = friendships.friend_pid AND fuser.pid <> user_info.pid) "
                    "WHERE user_info.pid = ? "
                    "ORDER BY fuser.pid;";

            if (!craftStatement(sqlCommand, &getFriendsInfoStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getFriendsInfoStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>((int64_t) query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
                      }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::BLOB, DBDataType::BLOB,
                                                   DBDataType::BLOB, DBDataType::DATETIME, DBDataType::DATETIME};

        if (!runStatement(getFriendsInfoStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getFriendsInfoStatement);
            sqlite3_clear_bindings(getFriendsInfoStatement);
            goto push_results;
        }

        sqlite3_reset(getFriendsInfoStatement);
        sqlite3_clear_bindings(getFriendsInfoStatement);

        std::vector<DBFriendInfoData> friendsData;
        for (const auto& row : *returnedData) {
            DBFriendInfoData friendInfo {
                .friendPid = (uint32_t) std::any_cast<int64_t>(row[0]->data),
                .showPresence = (bool) std::any_cast<int64_t>(row[1]->data),
                .showPlaying = (bool) std::any_cast<int64_t>(row[2]->data),
                .blockRequests = (bool) std::any_cast<int64_t>(row[3]->data),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>(row[4]->data),
                .presence = std::any_cast<std::vector<uint8_t>>(row[5]->data),
                .comment = std::any_cast<std::vector<uint8_t>>(row[6]->data),
                .lastOnline = std::any_cast<datetime_t>(row[7]->data),
                .becameFriends = std::any_cast<datetime_t>(row[8]->data)
            };

            friendsData.push_back(friendInfo);
        }

        resultsData = std::move(friendsData);
    } else if (command->type == DBCommandType::UPDATE_USER_INFO) {
        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        auto* updateData = std::any_cast<DBUserInfoUpdate>(&command->data);

        // Not all data needs to be updated, so we need to check which fields are being updated.
        // These are given by optionals, so we can just check if they have a value.
        std::string sqlCommand = "UPDATE user_info SET ";
        if (updateData->showPresence.has_value()) {
            sqlCommand += "show_presence = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->showPresence.value())));
        }

        if (updateData->showPlaying.has_value()) {
            sqlCommand += "show_playing = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->showPlaying.value())));
        }

        if (updateData->blockRequests.has_value()) {
            sqlCommand += "block_requests = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->blockRequests.value())));
        }

        if (updateData->nnaInfo.has_value()) {
            sqlCommand += "nna_info = ?, ";
            dataTypes.push_back(DBDataType::BLOB);
            data.emplace_back(std::make_shared<DBBlob>(updateData->nnaInfo.value()));
        }

        if (updateData->presence.has_value()) {
            sqlCommand += "presence = ?, ";
            dataTypes.push_back(DBDataType::BLOB);
            data.emplace_back(std::make_shared<DBBlob>(updateData->presence.value()));
        }

        if (updateData->comment.has_value()) {
            sqlCommand += "comment = ?, ";
            dataTypes.push_back(DBDataType::BLOB);
            data.emplace_back(std::make_shared<DBBlob>(updateData->comment.value()));
        }

        if (updateData->lastOnline.has_value()) {
            sqlCommand += "last_online = ?, ";
            dataTypes.push_back(DBDataType::DATETIME);
            data.emplace_back(std::make_shared<DBDateTime>(updateData->lastOnline.value()));
        }

        // Remove the last comma and space and add the WHERE clause
        sqlCommand = sqlCommand.substr(0, sqlCommand.size() - 2) + " WHERE pid = ?;";

        dataTypes.push_back(DBDataType::INTEGER);
        data.emplace_back(std::make_shared<DBInteger>((int64_t) updateData->pid));

        if (!craftStatement(sqlCommand, &statement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(statement, dataTypes, data)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(statement);
            goto push_results;
        }

        if (!runStatement(statement, dataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(statement);
            goto push_results;
        }

        sqlite3_finalize(statement);
    } else if (command->type == DBCommandType::INSERT_USER_INFO) {
        if (insertUserInfoStatement == nullptr) {
            if (!craftStatement("INSERT INTO user_info (pid, show_presence, show_playing, block_requests, nna_info, presence, comment, last_online) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?, ?);", &insertUserInfoStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
                                }
        }

        auto* userInfoData = std::any_cast<DBUserInfoData>(&command->data);

        if (!bindData(insertUserInfoStatement, {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                 DBDataType::INTEGER, DBDataType::BLOB, DBDataType::BLOB,
                                                 DBDataType::BLOB, DBDataType::DATETIME},
                      {std::make_shared<DBInteger>((int64_t) userInfoData->pid),
                       std::make_shared<DBInteger>((int64_t) userInfoData->showPresence),
                       std::make_shared<DBInteger>((int64_t) userInfoData->showPlaying),
                       std::make_shared<DBInteger>((int64_t) userInfoData->blockRequests),
                       std::make_shared<DBBlob>(userInfoData->nnaInfo),
                       std::make_shared<DBBlob>(userInfoData->presence),
                       std::make_shared<DBBlob>(userInfoData->comment),
                       std::make_shared<DBDateTime>(userInfoData->lastOnline)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertUserInfoStatement);
            goto push_results;
                       }

        if (!runStatement(insertUserInfoStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertUserInfoStatement);
            sqlite3_clear_bindings(insertUserInfoStatement);
            goto push_results;
        }

        sqlite3_reset(insertUserInfoStatement);
        sqlite3_clear_bindings(insertUserInfoStatement);
    } else if (command->type == DBCommandType::GET_USER_PROFILE) {
        if (getUserProfileStatement == nullptr) {
            std::string sqlCommand = "SELECT u.pid, u.username, u.email_id, u.mii_id, u.gender, u.region, u.tz,"
                                     "u.language, u.active, u.marketing, u.off_device, u.birth_date, u.country, u.create_date, u.last_updated,"
                                     "e.address AS email_address, e.parent AS email_parent, e.`primary` AS email_primary, "
                                     "e.reachable AS email_reachable, e.type AS email_type, e.updated_by AS email_updated_by, "
                                     "e.validated AS email_validated, e.validated_date AS email_validated_date, m.name AS mii_name, "
                                     "m.data AS mii_data, m.`primary` AS mii_primary, m.hash AS mii_hash "
                                     "FROM users u LEFT JOIN emails e ON u.email_id = e.id LEFT JOIN miis m ON u.mii_id = m.id "
                                     "WHERE u.pid = ?;";

            if (!craftStatement(sqlCommand, &getUserProfileStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getUserProfileStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>((int64_t) query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
                      }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::STRING, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::DATETIME, DBDataType::DATETIME,
                                                   DBDataType::STRING, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                   DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING};

        if (!runStatement(getUserProfileStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getUserProfileStatement);
            sqlite3_clear_bindings(getUserProfileStatement);
            goto push_results;
        }

        sqlite3_reset(getUserProfileStatement);
        sqlite3_clear_bindings(getUserProfileStatement);

        if (!returnedData->empty()) {
            DBUserProfileData profileData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .username = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .emailId = std::any_cast<int64_t>((*returnedData)[0][2]->data),
                .miiId = std::any_cast<int64_t>((*returnedData)[0][3]->data),
                .gender = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][4]->data)),
                .region = std::any_cast<int64_t>((*returnedData)[0][5]->data),
                .tz = std::any_cast<std::string>((*returnedData)[0][6]->data),
                .language = std::any_cast<std::string>((*returnedData)[0][7]->data),
                .active = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][8]->data)),
                .marketing = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][9]->data)),
                .offDevice = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][10]->data)),
                .birthdate = std::any_cast<std::string>((*returnedData)[0][11]->data),
                .country = std::any_cast<std::string>((*returnedData)[0][12]->data),
                .created = std::any_cast<datetime_t>((*returnedData)[0][13]->data),
                .updated = std::any_cast<datetime_t>((*returnedData)[0][14]->data),
                .email = std::any_cast<std::string>((*returnedData)[0][15]->data),
                .emailParent = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][16]->data)),
                .emailPrimary = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][17]->data)),
                .emailReachable = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][18]->data)),
                .emailType = std::any_cast<std::string>((*returnedData)[0][19]->data),
                .emailUpdatedBy = std::any_cast<std::string>((*returnedData)[0][20]->data),
                .emailValidated = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][21]->data)),
                .emailValidatedDate = std::any_cast<datetime_t>((*returnedData)[0][22]->data),
                .miiName = std::any_cast<std::string>((*returnedData)[0][23]->data),
                .miiData = std::any_cast<std::string>((*returnedData)[0][24]->data),
                .miiPrimary = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][25]->data)),
                .miiHash = std::any_cast<std::string>((*returnedData)[0][26]->data)
            };

            resultsData = std::move(profileData);
        }
    } else if (command->type == DBCommandType::GET_DEVICE_ATTRIBUTES) {
        if (getDeviceAttributesStatement == nullptr) {
            std::string sqlCommand = "SELECT * FROM device_attributes WHERE pid = ? AND device_id = ?;";

            if (!craftStatement(sqlCommand, &getDeviceAttributesStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBDeviceAttributesQuery>(&command->data);

        if (!bindData(getDeviceAttributesStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid)),
                       std::make_shared<DBInteger>(query->deviceId)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
                       }

        std::vector returnedDataTypes {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::DATETIME};

        if (!runStatement(getDeviceAttributesStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getDeviceAttributesStatement);
            sqlite3_clear_bindings(getDeviceAttributesStatement);
            goto push_results;
        }

        sqlite3_reset(getDeviceAttributesStatement);
        sqlite3_clear_bindings(getDeviceAttributesStatement);

        std::vector<DBDeviceAttributeData> attributesData;
        for (const auto& row : *returnedData) {
            DBDeviceAttributeData attributeData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                .name = std::any_cast<std::string>(row[2]->data),
                .value = std::any_cast<std::string>(row[3]->data),
                .createdDate = std::any_cast<datetime_t>(row[4]->data)
            };

            attributesData.push_back(attributeData);
        }

        resultsData = std::move(attributesData);
    } else if (command->type == DBCommandType::GET_AGREEMENT) {
        auto* query = std::any_cast<DBGetAgreementQuery>(&command->data);
        if (query->version.has_value() && getAgreementStatement == nullptr) {
            const std::string sqlCommand = "SELECT type, version, country, language, language_name, publish_date, "
                                           "main_title, sub_title, agree_text, non_agree_text, main_text, sub_text "
                                           "FROM agreements WHERE type = ? AND version = ? AND country = ? AND language = ?;";

            if (!craftStatement(sqlCommand, &getAgreementStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        } else if (!query->version.has_value() && getLatestAgreementStatement == nullptr) {
            const std::string sqlCommand = "SELECT type, version, country, language, language_name, publish_date, "
                                           "main_title, sub_title, agree_text, non_agree_text, main_text, sub_text "
                                           "FROM agreements WHERE type = ? AND country = ? AND language = ? "
                                           "ORDER BY version DESC LIMIT 1;";

            if (!craftStatement(sqlCommand, &getLatestAgreementStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        sqlite3_stmt* agreementStatement = query->version.has_value() ? getAgreementStatement : getLatestAgreementStatement;

        if (query->version.has_value() && !bindData(agreementStatement, {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING},
                      {std::make_shared<DBString>(query->type),
                       std::make_shared<DBInteger>(query->version.value()),
                       std::make_shared<DBString>(query->country),
                       std::make_shared<DBString>(query->language)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(agreementStatement);
            goto push_results;
                       }

        if (!query->version.has_value() && !bindData(agreementStatement, {DBDataType::STRING, DBDataType::STRING, DBDataType::STRING},
                                                     {std::make_shared<DBString>(query->type),
                                                         std::make_shared<DBString>(query->country),
                                                         std::make_shared<DBString>(query->language)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(agreementStatement);
            goto push_results;
                                                         }

        std::vector returnedDataTypes {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                       DBDataType::STRING, DBDataType::DATETIME, DBDataType::STRING, DBDataType::STRING,
                                       DBDataType::STRING, DBDataType::STRING, DBDataType::STRING, DBDataType::STRING};

        if (!runStatement(agreementStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(agreementStatement);
            sqlite3_clear_bindings(agreementStatement);
            goto push_results;
        }

        sqlite3_reset(agreementStatement);
        sqlite3_clear_bindings(agreementStatement);

        if (!returnedData->empty()) {
            DBAgreementData agreementData {};
            agreementData.type = std::any_cast<std::string>((*returnedData)[0][0]->data);
            agreementData.version = static_cast<int>(std::any_cast<int64_t>((*returnedData)[0][1]->data));
            agreementData.country = std::any_cast<std::string>((*returnedData)[0][2]->data);
            agreementData.language = std::any_cast<std::string>((*returnedData)[0][3]->data);
            agreementData.languageName = std::any_cast<std::string>((*returnedData)[0][4]->data);
            agreementData.publishedAt = std::any_cast<datetime_t>((*returnedData)[0][5]->data);
            agreementData.mainTitle = std::any_cast<std::string>((*returnedData)[0][6]->data);
            agreementData.subTitle = std::any_cast<std::string>((*returnedData)[0][7]->data);
            agreementData.agreeText = std::any_cast<std::string>((*returnedData)[0][8]->data);
            agreementData.disagreeText = std::any_cast<std::string>((*returnedData)[0][9]->data);
            agreementData.mainText = std::any_cast<std::string>((*returnedData)[0][10]->data);
            agreementData.subText = std::any_cast<std::string>((*returnedData)[0][11]->data);

            resultsData = std::move(agreementData);
        }
    } else if (command->type == DBCommandType::GET_DEVICE) {
        if (getDeviceStatement == nullptr) {
            std::string sqlCommand = "SELECT id, language, platform_id, region, serial_num, system_ver, type, "
                                     "updated_by, last_updated FROM devices WHERE id = ?;";

            if (!craftStatement(sqlCommand, &getDeviceStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getDeviceStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)}) ) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getDeviceStatement);
            goto push_results;
        }

        if (!runStatement(getDeviceStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                DBDataType::STRING, DBDataType::STRING, DBDataType::DATETIME},
                                                returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getDeviceStatement);
            sqlite3_clear_bindings(getDeviceStatement);
            goto push_results;
        }

        if (!returnedData->empty()) {
            DBDeviceData deviceData {
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .language = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .platformId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][2]->data)),
                .region = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][3]->data)),
                .serialNumber = std::any_cast<std::string>((*returnedData)[0][4]->data),
                .systemVersion = std::any_cast<std::string>((*returnedData)[0][5]->data),
                .type = std::any_cast<std::string>((*returnedData)[0][6]->data),
                .updatedBy = std::any_cast<std::string>((*returnedData)[0][7]->data),
                .lastUpdated = std::any_cast<datetime_t>((*returnedData)[0][8]->data)
            };

            resultsData = std::move(deviceData);
        }

        sqlite3_reset(getDeviceStatement);
        sqlite3_clear_bindings(getDeviceStatement);
    } else if (command->type == DBCommandType::GET_LATEST_PID) {
        if (getLatestPIDStatement == nullptr) {
            // New PIDS start at 1799999999 and go downwards, so we can just get the latest PID
            std::string sqlCommand = "SELECT pid FROM users ORDER BY pid ASC LIMIT 1;";

            if (!craftStatement(sqlCommand, &getLatestPIDStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!runStatement(getLatestPIDStatement, {DBDataType::INTEGER}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getLatestPIDStatement);
            sqlite3_clear_bindings(getLatestPIDStatement);
            goto push_results;
        }

        uint32_t newPid = 1800000000; // Default value if no users exist
        if (!returnedData->empty()) {
            newPid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data));
        }

        sqlite3_reset(getLatestPIDStatement);
        sqlite3_clear_bindings(getLatestPIDStatement);

        resultsData = newPid;
    } else if (command->type == DBCommandType::GET_OWNERSHIP) {
        if (getOwnershipStatement == nullptr) {
            std::string sqlCommand = "SELECT pid, device_id, status, last_updated FROM ownerships WHERE pid = ? "
                                     "AND device_id = ?;";

            if (!craftStatement(sqlCommand, &getOwnershipStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBOwnershipQuery>(&command->data);

        if (!bindData(getOwnershipStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->deviceId)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getOwnershipStatement);
            goto push_results;
        }

        if (!runStatement(getOwnershipStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::STRING, DBDataType::DATETIME}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getOwnershipStatement);
            sqlite3_clear_bindings(getOwnershipStatement);
            goto push_results;
        }

        if (!returnedData->empty()) {
            DBOwnershipData ownershipData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][1]->data)),
                .status = std::any_cast<std::string>((*returnedData)[0][2]->data),
                .lastUpdated = std::any_cast<datetime_t>((*returnedData)[0][3]->data)
            };

            resultsData = std::move(ownershipData);
        }

        sqlite3_reset(getOwnershipStatement);
        sqlite3_clear_bindings(getOwnershipStatement);
    } else if (command->type == DBCommandType::GET_LATEST_OWNERSHIP) {
        if (getLatestOwnershipStatement == nullptr) {
            std::string sqlCommand = "SELECT pid, device_id, status, last_updated FROM ownerships "
                                     "WHERE pid = ? ORDER BY last_updated DESC LIMIT 1;";

            if (!craftStatement(sqlCommand, &getLatestOwnershipStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getLatestOwnershipStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getLatestOwnershipStatement);
            goto push_results;
        }

        if (!runStatement(getLatestOwnershipStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                        DBDataType::STRING, DBDataType::DATETIME}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getLatestOwnershipStatement);
            sqlite3_clear_bindings(getLatestOwnershipStatement);
            goto push_results;
        }

        if (!returnedData->empty()) {
            DBOwnershipData ownershipData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][1]->data)),
                .status = std::any_cast<std::string>((*returnedData)[0][2]->data),
                .lastUpdated = std::any_cast<datetime_t>((*returnedData)[0][3]->data)
            };

            resultsData = std::move(ownershipData);
        }

        sqlite3_reset(getLatestOwnershipStatement);
        sqlite3_clear_bindings(getLatestOwnershipStatement);
    } else if (command->type == DBCommandType::HAS_ACTIVE_OWNERSHIP) {
        if (hasActiveOwnershipStatement == nullptr) {
            std::string sqlCommand = "SELECT COUNT(*) FROM ownerships WHERE pid = ? AND status = 'ACTIVE';";

            if (!craftStatement(sqlCommand, &hasActiveOwnershipStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(hasActiveOwnershipStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(hasActiveOwnershipStatement);
            goto push_results;
        }

        if (!runStatement(hasActiveOwnershipStatement, {DBDataType::INTEGER}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(hasActiveOwnershipStatement);
            sqlite3_clear_bindings(hasActiveOwnershipStatement);
            goto push_results;
        }

        if (!returnedData->empty()) {
            // The result is a count of active ownerships, so we can just return the first value
            uint32_t count = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data));
            resultsData = count > 0;
        } else {
            resultsData = false; // No active ownerships found
        }

        sqlite3_reset(hasActiveOwnershipStatement);
        sqlite3_clear_bindings(hasActiveOwnershipStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_DEVICE) {
        if (insertOrUpdateDeviceStatement == nullptr) {
            std::string sqlCommand = "INSERT OR REPLACE INTO devices (id, language, platform_id, region, serial_num, "
                                     "system_ver, type, updated_by, last_updated) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertOrUpdateDeviceStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBDeviceInsertOrUpdateQuery>(&command->data);

        if (!bindData(insertOrUpdateDeviceStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                      DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                      DBDataType::STRING, DBDataType::STRING, DBDataType::DATETIME},
                      {std::make_shared<DBInteger>(query->deviceId),
                       std::make_shared<DBString>(query->language),
                       std::make_shared<DBInteger>(query->platformId),
                       std::make_shared<DBInteger>(query->region),
                       std::make_shared<DBString>(query->serialNumber),
                       std::make_shared<DBString>(query->systemVersion),
                       std::make_shared<DBString>(query->type),
                       std::make_shared<DBString>(query->updatedBy),
                       std::make_shared<DBDateTime>(query->lastUpdated)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateDeviceStatement);
            goto push_results;
                       }

        if (!runStatement(insertOrUpdateDeviceStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateDeviceStatement);
            sqlite3_clear_bindings(insertOrUpdateDeviceStatement);
            goto push_results;
        }

        sqlite3_reset(insertOrUpdateDeviceStatement);
        sqlite3_clear_bindings(insertOrUpdateDeviceStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_USER_AGREEMENT) {
        if (insertOrUpdateUserAgreementStatement == nullptr) {
            std::string sqlCommand = "INSERT OR REPLACE INTO user_agreements (pid, type, version, country, signed_date) VALUES (?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertOrUpdateUserAgreementStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBUserAgreementInsertOrUpdateQuery>(&command->data);

        if (!bindData(insertOrUpdateUserAgreementStatement, {DBDataType::INTEGER, DBDataType::STRING,
                                                            DBDataType::INTEGER, DBDataType::STRING, DBDataType::DATETIME},
                                                      {std::make_shared<DBInteger>(query->pid),
                                                       std::make_shared<DBString>(query->type),
                                                       std::make_shared<DBInteger>(query->version),
                                                       std::make_shared<DBString>(query->country),
                                                       std::make_shared<DBDateTime>(query->signedAt)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateUserAgreementStatement);
            goto push_results;
                                                       }

        if (!runStatement(insertOrUpdateUserAgreementStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateUserAgreementStatement);
            sqlite3_clear_bindings(insertOrUpdateUserAgreementStatement);
            goto push_results;
        }

        sqlite3_reset(insertOrUpdateUserAgreementStatement);
        sqlite3_clear_bindings(insertOrUpdateUserAgreementStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_MII) {
        if (insertOrUpdateMiiStatement == nullptr) {
            std::string sqlCommand = "INSERT OR REPLACE INTO miis (id, hash, name, `primary`, data) VALUES (?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertOrUpdateMiiStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBMiiInsertOrUpdateQuery>(&command->data);

        std::shared_ptr<DBData> miiIdData = std::make_shared<DBNull>();
        if (query->miiId.has_value()) {
            miiIdData = std::make_shared<DBInteger>(query->miiId.value());
        }

        if (!bindData(insertOrUpdateMiiStatement, {miiIdData->type, DBDataType::STRING, DBDataType::STRING,
                                                   DBDataType::INTEGER, DBDataType::STRING},
                      {miiIdData,
                       std::make_shared<DBString>(query->hash),
                       std::make_shared<DBString>(query->name),
                       std::make_shared<DBInteger>(static_cast<int64_t>(query->primary)),
                       std::make_shared<DBString>(query->data)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateMiiStatement);
            goto push_results;
                       }

        if (!runStatement(insertOrUpdateMiiStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateMiiStatement);
            sqlite3_clear_bindings(insertOrUpdateMiiStatement);
            goto push_results;
        }

        int64_t insertedId = query->miiId.has_value() ? query->miiId.value() : sqlite3_last_insert_rowid(db);
        if (insertedId < 0) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateEmailStatement);
            sqlite3_clear_bindings(insertOrUpdateEmailStatement);
            goto push_results;
        }

        resultsData = insertedId;

        sqlite3_reset(insertOrUpdateMiiStatement);
        sqlite3_clear_bindings(insertOrUpdateMiiStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_EMAIL) {
        if (insertOrUpdateEmailStatement == nullptr) {
            std::string sqlCommand = "INSERT OR REPLACE INTO emails (id, address, parent, `primary`, reachable, type, updated_by, validated, validated_date) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertOrUpdateEmailStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBEmailInsertOrUpdateQuery>(&command->data);

        std::shared_ptr<DBData> emailIdData = std::make_shared<DBNull>();
        if (query->emailId.has_value()) {
            emailIdData = std::make_shared<DBInteger>(query->emailId.value());
        }

        if (!bindData(insertOrUpdateEmailStatement, {emailIdData->type, DBDataType::STRING, DBDataType::INTEGER,
                                                     DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                     DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME},
                                                      {emailIdData,
                                                       std::make_shared<DBString>(query->email),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->parent)),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->primary)),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->reachable)),
                                                       std::make_shared<DBString>(query->type),
                                                       std::make_shared<DBString>(query->updatedBy),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->validated)),
                                                       std::make_shared<DBDateTime>(query->validatedAt)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateEmailStatement);
            goto push_results;
                                                       }

        if (!runStatement(insertOrUpdateEmailStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateEmailStatement);
            sqlite3_clear_bindings(insertOrUpdateEmailStatement);
            goto push_results;
        }

        int64_t insertedId = query->emailId.has_value() ? query->emailId.value() : sqlite3_last_insert_rowid(db);
        if (insertedId < 0) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateEmailStatement);
            sqlite3_clear_bindings(insertOrUpdateEmailStatement);
            goto push_results;
        }

        resultsData = insertedId;

        sqlite3_reset(insertOrUpdateEmailStatement);
        sqlite3_clear_bindings(insertOrUpdateEmailStatement);
    } else if (command->type == DBCommandType::INSERT_USER_PROFILE) {
        if (insertProfileStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO users (pid, username, password, email_id, mii_id, gender, region, tz, "
                                     "language, active, marketing, off_device, birth_date, country, create_date, last_updated) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertProfileStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBUserProfileInsertQuery>(&command->data);

        if (!bindData(insertProfileStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                 DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                 DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                 DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                 DBDataType::STRING, DBDataType::STRING, DBDataType::DATETIME,
                                                 DBDataType::DATETIME},
                                                  {std::make_shared<DBInteger>(query->pid),
                                                   std::make_shared<DBString>(query->username),
                                                   std::make_shared<DBString>(query->password),
                                                   std::make_shared<DBInteger>(query->emailId),
                                                   std::make_shared<DBInteger>(query->miiId),
                                                   std::make_shared<DBInteger>(static_cast<int64_t>(query->gender)),
                                                   std::make_shared<DBInteger>(query->region),
                                                   std::make_shared<DBString>(query->tz),
                                                   std::make_shared<DBString>(query->language),
                                                   std::make_shared<DBInteger>(static_cast<int64_t>(query->active)),
                                                   std::make_shared<DBInteger>(static_cast<int64_t>(query->marketing)),
                                                   std::make_shared<DBInteger>(static_cast<int64_t>(query->offDevice)),
                                                   std::make_shared<DBString>(query->birthdate),
                                                   std::make_shared<DBString>(query->country),
                                                   std::make_shared<DBDateTime>(query->created),
                                                   std::make_shared<DBDateTime>(query->updated)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertProfileStatement);
            goto push_results;
                                                   }

        if (!runStatement(insertProfileStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertProfileStatement);
            sqlite3_clear_bindings(insertProfileStatement);
            goto push_results;
        }

        sqlite3_reset(insertProfileStatement);
        sqlite3_clear_bindings(insertProfileStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_DEVICE_ATTRIBUTES) {
        if (insertOrUpdateDeviceAttributesStatement == nullptr) {
            std::string sqlCommand = "INSERT OR REPLACE INTO device_attributes (device_id, pid, name, value, created_date) "
                                     "VALUES (?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertOrUpdateDeviceAttributesStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBDeviceAttributesInsertOrUpdateQuery>(&command->data);

        if (!bindData(insertOrUpdateDeviceAttributesStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                               DBDataType::STRING, DBDataType::STRING, DBDataType::DATETIME},
                                                      {std::make_shared<DBInteger>(query->deviceId),
                                                       std::make_shared<DBInteger>(query->pid),
                                                       std::make_shared<DBString>(query->name),
                                                       std::make_shared<DBString>(query->value),
                                                       std::make_shared<DBDateTime>(query->createdDate)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateDeviceAttributesStatement);
            goto push_results;
                                                       }

        if (!runStatement(insertOrUpdateDeviceAttributesStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateDeviceAttributesStatement);
            sqlite3_clear_bindings(insertOrUpdateDeviceAttributesStatement);
            goto push_results;
        }

        sqlite3_reset(insertOrUpdateDeviceAttributesStatement);
        sqlite3_clear_bindings(insertOrUpdateDeviceAttributesStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_OWNERSHIP) {
        if (insertOrUpdateOwnershipStatement == nullptr) {
            std::string sqlCommand = "INSERT OR REPLACE INTO ownerships (pid, device_id, status, last_updated) "
                                     "VALUES (?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertOrUpdateOwnershipStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBOwnershipInsertOrUpdateQuery>(&command->data);

        if (!bindData(insertOrUpdateOwnershipStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                         DBDataType::STRING, DBDataType::DATETIME},
                                                      {std::make_shared<DBInteger>(query->pid),
                                                       std::make_shared<DBInteger>(query->deviceId),
                                                       std::make_shared<DBString>(query->status),
                                                       std::make_shared<DBDateTime>(query->lastUpdated)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateOwnershipStatement);
            goto push_results;
                                                       }

        if (!runStatement(insertOrUpdateOwnershipStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateOwnershipStatement);
            sqlite3_clear_bindings(insertOrUpdateOwnershipStatement);
            goto push_results;
        }

        sqlite3_reset(insertOrUpdateOwnershipStatement);
        sqlite3_clear_bindings(insertOrUpdateOwnershipStatement);
    } else if (command->type == DBCommandType::UPDATE_USER_PROFILE) {
        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        auto* updateData = std::any_cast<DBUserProfileUpdateQuery>(&command->data);

        // Not all data needs to be updated, so we need to check which fields are being updated.
        // These are given by optionals, so we can just check if they have a value.
        std::string sqlCommand = "UPDATE users SET ";
        if (updateData->username.has_value()) {
            sqlCommand += "username = ?, ";
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(updateData->username.value()));
        }

        if (updateData->password.has_value()) {
            sqlCommand += "password = ?, ";
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(updateData->password.value()));
        }

        if (updateData->emailId.has_value()) {
            sqlCommand += "email_id = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(updateData->emailId.value()));
        }

        if (updateData->miiId.has_value()) {
            sqlCommand += "mii_id = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(updateData->miiId.value()));
        }

        if (updateData->gender.has_value()) {
            sqlCommand += "gender = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->gender.value())));
        }

        if (updateData->region.has_value()) {
            sqlCommand += "region = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(updateData->region.value()));
        }

        if (updateData->tz.has_value()) {
            sqlCommand += "tz = ?, ";
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(updateData->tz.value()));
        }

        if (updateData->language.has_value()) {
            sqlCommand += "language = ?, ";
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(updateData->language.value()));
        }

        if (updateData->active.has_value()) {
            sqlCommand += "active = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->active.value())));
        }

        if (updateData->marketing.has_value()) {
            sqlCommand += "marketing = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->marketing.value())));
        }

        if (updateData->offDevice.has_value()) {
            sqlCommand += "off_device = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->offDevice.value())));
        }

        if (updateData->birthdate.has_value()) {
            sqlCommand += "birth_date = ?, ";
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(updateData->birthdate.value()));
        }

        if (updateData->country.has_value()) {
            sqlCommand += "country = ?, ";
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(updateData->country.value()));
        }

        if (updateData->created.has_value()) {
            sqlCommand += "create_date = ?, ";
            dataTypes.push_back(DBDataType::DATETIME);
            data.emplace_back(std::make_shared<DBDateTime>(updateData->created.value()));
        }

        if (updateData->updated.has_value()) {
            sqlCommand += "last_updated = ?, ";
            dataTypes.push_back(DBDataType::DATETIME);
            data.emplace_back(std::make_shared<DBDateTime>(updateData->updated.value()));
        }

        // Remove the last comma and space and add the WHERE clause
        sqlCommand = sqlCommand.substr(0, sqlCommand.size() - 2) + " WHERE pid = ?;";

        dataTypes.push_back(DBDataType::INTEGER);
        data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->pid)));

        if (!craftStatement(sqlCommand, &statement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(statement, dataTypes, data)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(statement);
            goto push_results;
        }

        if (!runStatement(statement, dataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(statement);
            goto push_results;
        }

        sqlite3_finalize(statement);
    } else if (command->type == DBCommandType::DELETE_EMAIL) {
        if (deleteEmailStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM emails WHERE id = ?;";

            if (!craftStatement(sqlCommand, &deleteEmailStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBIdQuery>(&command->data);

        if (!bindData(deleteEmailStatement, {DBDataType::INTEGER},
                                                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteEmailStatement);
            goto push_results;
        }

        if (!runStatement(deleteEmailStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(deleteEmailStatement);
            sqlite3_clear_bindings(deleteEmailStatement);
            goto push_results;
        }

        sqlite3_reset(deleteEmailStatement);
        sqlite3_clear_bindings(deleteEmailStatement);
    } else if (command->type == DBCommandType::DELETE_MII) {
        if (deleteMiiStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM miis WHERE id = ?;";

            if (!craftStatement(sqlCommand, &deleteMiiStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBIdQuery>(&command->data);

        if (!bindData(deleteMiiStatement, {DBDataType::INTEGER},
                                                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteMiiStatement);
            goto push_results;
                                                      }

        if (!runStatement(deleteMiiStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(deleteMiiStatement);
            sqlite3_clear_bindings(deleteMiiStatement);
            goto push_results;
        }

        sqlite3_reset(deleteMiiStatement);
        sqlite3_clear_bindings(deleteMiiStatement);
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Unknown command type: " + std::to_string(static_cast<int>(command->type)));
        return;
    }

    push_results:
    auto result = Result(resultStatus, std::move(resultsData));
    command->task->complete(std::move(result));
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

bool sqlite3Database::bindData(sqlite3_stmt *statement, const std::vector<DBDataType>& dataTypes,
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
                                                         std::string(sqlite3_expanded_sql(statement)));

    int result = sqlite3_step(statement);
    while (result != SQLITE_DONE) {
        if (result == SQLITE_ROW && returnedData != nullptr) {
            returnedData->emplace_back();
            for (int i = 0; i < sqlite3_column_count(statement); i++) {
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
        } else {
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

    if (!isSession) {
        if (dbThreadHandle->joinable()) {
            *shouldStop = true;
            dbQueueCV->notify_all();
            dbThreadHandle->join();

            logger->log(Logger::level::INFO, Logger::group::DB, "SQLite 3 database thread stopped.");
        }

        std::unique_lock queueLock(*commandQueueMutex);
        while (!commandQueue->empty()) {
            commandQueue->pop();
        }
        queueLock.unlock();
    } else {
        logger->log(Logger::level::DEBUG, Logger::group::DB, "Closing SQLite 3 database session.");
    }

    if (getUserByPIDStatement != nullptr) sqlite3_finalize(getUserByPIDStatement);
    if (getUserByUsernameStatement != nullptr) sqlite3_finalize(getUserByUsernameStatement);
    if (getGameServerAccessStatement != nullptr) sqlite3_finalize(getGameServerAccessStatement);
    if (insertGameServerAccessStatement != nullptr) sqlite3_finalize(insertGameServerAccessStatement);
    if (getUserInfoStatement != nullptr) sqlite3_finalize(getUserInfoStatement);
    if (insertUserInfoStatement != nullptr) sqlite3_finalize(insertUserInfoStatement);
    if (getFriendsInfoStatement != nullptr) sqlite3_finalize(getFriendsInfoStatement);
    if (getUserProfileStatement != nullptr) sqlite3_finalize(getUserProfileStatement);
    if (getDeviceAttributesStatement != nullptr) sqlite3_finalize(getDeviceAttributesStatement);
    if (getAgreementStatement != nullptr) sqlite3_finalize(getAgreementStatement);
    if (getDeviceStatement != nullptr) sqlite3_finalize(getDeviceStatement);
    if (getLatestPIDStatement != nullptr) sqlite3_finalize(getLatestPIDStatement);
    if (getOwnershipStatement != nullptr) sqlite3_finalize(getOwnershipStatement);
    if (getLatestOwnershipStatement != nullptr) sqlite3_finalize(getLatestOwnershipStatement);
    if (getLatestAgreementStatement != nullptr) sqlite3_finalize(getLatestAgreementStatement);
    if (hasActiveOwnershipStatement != nullptr) sqlite3_finalize(hasActiveOwnershipStatement);
    if (insertOrUpdateDeviceStatement != nullptr) sqlite3_finalize(insertOrUpdateDeviceStatement);
    if (insertOrUpdateUserAgreementStatement != nullptr) sqlite3_finalize(insertOrUpdateUserAgreementStatement);
    if (insertOrUpdateMiiStatement != nullptr) sqlite3_finalize(insertOrUpdateMiiStatement);
    if (insertOrUpdateEmailStatement != nullptr) sqlite3_finalize(insertOrUpdateEmailStatement);
    if (insertProfileStatement != nullptr) sqlite3_finalize(insertProfileStatement);
    if (insertOrUpdateDeviceAttributesStatement != nullptr) sqlite3_finalize(insertOrUpdateDeviceAttributesStatement);
    if (insertOrUpdateOwnershipStatement != nullptr) sqlite3_finalize(insertOrUpdateOwnershipStatement);
    if (deleteMiiStatement != nullptr) sqlite3_finalize(deleteMiiStatement);
    if (deleteEmailStatement != nullptr) sqlite3_finalize(deleteEmailStatement);

    sqlite3_close(db);
    db = nullptr;
}

} // namespace db
