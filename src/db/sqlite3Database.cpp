#include <memory>
#include <utility>
#include <chrono>
#include <date/date.h>

#include "sqlite3Database.hpp"
#include "../util/util.hpp"

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

    std::string foreignKeysSql = "PRAGMA foreign_keys = ON;";
    sqlite3_stmt* foreignKeysStmt;
    if (!craftStatement(foreignKeysSql, &foreignKeysStmt)) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to craft foreign keys statement: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        return false;
    }

    if (runStatement(foreignKeysStmt, {}, nullptr)) {
        logger->log(Logger::level::DEBUG, Logger::group::DB, "Foreign keys enabled for SQLite 3 database.");
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to enable foreign keys: " + std::string(sqlite3_errmsg(db)));
        sqlite3_finalize(foreignKeysStmt);
        sqlite3_close(db);
        return false;
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

async::ManualTask<Result> sqlite3Database::queueCommand(std::unique_ptr<Command> command) {
    if (*shouldStop) return async::ManualTask<Result>();

    auto task = std::make_shared<async::ManualTask<Result>>();

    command->task = task;
    command->db = this->shared_from_this();

    std::unique_lock lock(*commandQueueMutex);
    commandQueue->emplace(std::move(command));

    return *task;
}

async::ManualTask<Result> sqlite3Database::startTransaction() {
    auto command = craftVoidCommand("BEGIN TRANSACTION;");
    auto task = std::move(queueCommand(std::move(command)));

    processQueue();

    return std::move(task);
}

async::ManualTask<Result> sqlite3Database::commitTransaction() {
    auto command = craftVoidCommand("COMMIT TRANSACTION;");
    auto task = std::move(queueCommand(std::move(command)));

    processQueue();

    return std::move(task);
}

async::ManualTask<Result> sqlite3Database::rollbackTransaction() {
    auto command = craftVoidCommand("ROLLBACK TRANSACTION;");
    auto task = std::move(queueCommand(std::move(command)));

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
    } else if (command->type == DBCommandType::GET_USER_INFO_BY_PID) {
        if (getUserInfoByPidStatement == nullptr) {
            if (!craftStatement("SELECT pid, username, show_presence, show_playing, block_requests, "
                                "nna_info, presence, comment, last_online FROM user_info WHERE pid = ?;", &getUserInfoByPidStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getUserInfoByPidStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>((int64_t) query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
                      }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::BLOB,
                                                   DBDataType::BLOB, DBDataType::BLOB, DBDataType::DATETIME};

        if (!runStatement(getUserInfoByPidStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getUserInfoByPidStatement);
            sqlite3_clear_bindings(getUserInfoByPidStatement);
            goto push_results;
        }

        sqlite3_reset(getUserInfoByPidStatement);
        sqlite3_clear_bindings(getUserInfoByPidStatement);

        if (!returnedData->empty()) {
            DBUserInfoData userInfoData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .username = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .showPresence = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][2]->data)),
                .showPlaying = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][3]->data)),
                .blockRequests = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][4]->data)),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][5]->data),
                .presence = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][6]->data),
                .comment = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][7]->data),
                .lastOnline = std::any_cast<datetime_t>((*returnedData)[0][8]->data)
            };

            resultsData = std::move(userInfoData);
        }
    } else if (command->type == DBCommandType::GET_USER_INFO_BY_USERNAME) {
        if (getUserInfoByUsernameStatement == nullptr) {
            if (!craftStatement("SELECT pid, username, show_presence, show_playing, block_requests, "
                                "nna_info, presence, comment, last_online FROM user_info WHERE username = ?;", &getUserInfoByUsernameStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBUsernameQuery>(&command->data);

        if (!bindData(getUserInfoByUsernameStatement, {DBDataType::STRING},
                      {std::make_shared<DBString>(query->username)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
        }

        std::vector returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                       DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::BLOB,
                                       DBDataType::BLOB, DBDataType::BLOB, DBDataType::DATETIME};

        if (!runStatement(getUserInfoByUsernameStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getUserInfoByUsernameStatement);
            sqlite3_clear_bindings(getUserInfoByUsernameStatement);
            goto push_results;
        }

        sqlite3_reset(getUserInfoByUsernameStatement);
        sqlite3_clear_bindings(getUserInfoByUsernameStatement);

        if (!returnedData->empty()) {
            DBUserInfoData userInfoData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .username = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .showPresence = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][2]->data)),
                .showPlaying = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][3]->data)),
                .blockRequests = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][4]->data)),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][5]->data),
                .presence = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][6]->data),
                .comment = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][7]->data),
                .lastOnline = std::any_cast<datetime_t>((*returnedData)[0][8]->data)
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
            sqlite3_clear_bindings(getFriendsInfoStatement);
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
    } else if (command->type == DBCommandType::GET_FRIEND_REQUEST) {
        if (getFriendRequestStatement == nullptr) {
            if (!craftStatement("SELECT id, from_pid, to_pid, expiration, created_at, data"
                                " FROM friend_requests WHERE id = ?;",
                                &getFriendRequestStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBIdQuery>(&command->data);

        if (!bindData(getFriendRequestStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getFriendRequestStatement);
            goto push_results;
        }

        if (!runStatement(getFriendRequestStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                     DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::DATETIME,
                                                     DBDataType::BLOB}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getFriendRequestStatement);
            sqlite3_clear_bindings(getFriendRequestStatement);
            goto push_results;
        }

        sqlite3_reset(getFriendRequestStatement);
        sqlite3_clear_bindings(getFriendRequestStatement);

        if (!returnedData->empty()) {
            DBFriendRequestData friendRequestData {
                .id = std::any_cast<int64_t>((*returnedData)[0][0]->data),
                .fromPid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][1]->data)),
                .toPid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][2]->data)),
                .expiresAt = std::any_cast<datetime_t>((*returnedData)[0][3]->data),
                .createdAt = std::any_cast<datetime_t>((*returnedData)[0][4]->data),
                .data = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][5]->data)
            };

            resultsData = std::move(friendRequestData);
        }
    } else if (command->type == DBCommandType::GET_SENT_FRIEND_REQUESTS) {
        if (getSentFriendRequestsStatement == nullptr) {
            if (!craftStatement("SELECT r.id, r.from_pid, r.to_pid, r.expiration, r.created_at, r.data, u.nna_info "
                                "FROM friend_requests AS r "
                                "JOIN user_info AS u ON r.to_pid = u.pid "
                                "WHERE r.from_pid = ?;",
                                &getSentFriendRequestsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getSentFriendRequestsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getSentFriendRequestsStatement);
            goto push_results;
        }

        if (!runStatement(getSentFriendRequestsStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                     DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::DATETIME,
                                                     DBDataType::BLOB, DBDataType::BLOB}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getSentFriendRequestsStatement);
            sqlite3_clear_bindings(getSentFriendRequestsStatement);
            goto push_results;
        }

        sqlite3_reset(getSentFriendRequestsStatement);
        sqlite3_clear_bindings(getSentFriendRequestsStatement);

        std::vector<DBFriendRequestData> friendRequests;
        for (const auto& row : *returnedData) {
            DBFriendRequestData friendRequestData {
                .id = std::any_cast<int64_t>(row[0]->data),
                .fromPid = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                .toPid = static_cast<uint32_t>(std::any_cast<int64_t>(row[2]->data)),
                .expiresAt = std::any_cast<datetime_t>(row[3]->data),
                .createdAt = std::any_cast<datetime_t>(row[4]->data),
                .data = std::any_cast<std::vector<uint8_t>>(row[5]->data),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>(row[6]->data)
            };
            friendRequests.push_back(friendRequestData);
        }

        resultsData = std::move(friendRequests);
    } else if (command->type == DBCommandType::GET_RECEIVED_FRIEND_REQUESTS) {
        if (getReceivedFriendRequestsStatement == nullptr) {
            if (!craftStatement("SELECT r.id, r.from_pid, r.to_pid, r.expiration, r.created_at, r.data, u.nna_info "
                                "FROM friend_requests AS r "
                                "JOIN user_info AS u ON r.from_pid = u.pid "
                                "WHERE r.to_pid = ?;",
                                &getReceivedFriendRequestsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getReceivedFriendRequestsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getReceivedFriendRequestsStatement);
            goto push_results;
        }

        if (!runStatement(getReceivedFriendRequestsStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                     DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::DATETIME,
                                                     DBDataType::BLOB, DBDataType::BLOB}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getReceivedFriendRequestsStatement);
            sqlite3_clear_bindings(getReceivedFriendRequestsStatement);
            goto push_results;
        }

        sqlite3_reset(getReceivedFriendRequestsStatement);
        sqlite3_clear_bindings(getReceivedFriendRequestsStatement);

        std::vector<DBFriendRequestData> friendRequests;
        for (const auto& row : *returnedData) {
            DBFriendRequestData friendRequestData {
                .id = std::any_cast<int64_t>(row[0]->data),
                .fromPid = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                .toPid = static_cast<uint32_t>(std::any_cast<int64_t>(row[2]->data)),
                .expiresAt = std::any_cast<datetime_t>(row[3]->data),
                .createdAt = std::any_cast<datetime_t>(row[4]->data),
                .data = std::any_cast<std::vector<uint8_t>>(row[5]->data),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>(row[6]->data)
            };
            friendRequests.push_back(friendRequestData);
        }

        resultsData = std::move(friendRequests);
    } else if (command->type == DBCommandType::UPDATE_USER_INFO) {
        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        auto* updateData = std::any_cast<DBUserInfoUpdate>(&command->data);

        // Not all data needs to be updated, so we need to check which fields are being updated.
        // These are given by optionals, so we can just check if they have a value.
        std::string sqlCommand = "UPDATE user_info SET ";
        if (updateData->username.has_value()) {
            sqlCommand += "username = ?, ";
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(updateData->username.value()));
        }

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
    } else if (command->type == DBCommandType::INSERT_USER_INFO) {
        if (insertUserInfoStatement == nullptr) {
            if (!craftStatement("INSERT INTO user_info (pid, username, show_presence, show_playing, block_requests, nna_info, presence, comment, last_online) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);", &insertUserInfoStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* userInfoData = std::any_cast<DBUserInfoData>(&command->data);

        if (!bindData(insertUserInfoStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                 DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::BLOB, DBDataType::BLOB,
                                                 DBDataType::BLOB, DBDataType::DATETIME},
                      {std::make_shared<DBInteger>(static_cast<int64_t>(userInfoData->pid)),
                          std::make_shared<DBString>(userInfoData->username),
                       std::make_shared<DBInteger>(static_cast<int64_t>(userInfoData->showPresence)),
                       std::make_shared<DBInteger>(static_cast<int64_t>(userInfoData->showPlaying)),
                       std::make_shared<DBInteger>(static_cast<int64_t>(userInfoData->blockRequests)),
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
                                     "e.validated AS email_validated, e.validated_date AS email_validated_date, "
                                     "e.validation_code AS email_validation_code, m.name AS mii_name, "
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
                                                   DBDataType::STRING, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::STRING};

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
                .emailValidationCode = std::any_cast<std::string>((*returnedData)[0][23]->data),
                .miiName = std::any_cast<std::string>((*returnedData)[0][24]->data),
                .miiData = std::any_cast<std::string>((*returnedData)[0][25]->data),
                .miiPrimary = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][26]->data)),
                .miiHash = std::any_cast<std::string>((*returnedData)[0][27]->data)
            };

            resultsData = std::move(profileData);
        }
    } else if (command->type == DBCommandType::GET_USER_MII) {
        if (getUserMiiStatement == nullptr) {
            std::string sqlCommand = "SELECT u.username, u.mii_id, m.name AS mii_name, m.data AS mii_data, "
                                     "m.`primary` AS mii_primary, m.hash AS mii_hash "
                                     "FROM users u LEFT JOIN miis m ON u.mii_id = m.id WHERE u.pid = ?;";

            if (!craftStatement(sqlCommand, &getUserMiiStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getUserMiiStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
        }

        if (!runStatement(getUserMiiStatement, {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getUserMiiStatement);
            sqlite3_clear_bindings(getUserMiiStatement);
            goto push_results;
        }

        sqlite3_reset(getUserMiiStatement);
        sqlite3_clear_bindings(getUserMiiStatement);

        if (!returnedData->empty()) {
            DBUserMii userMii {
                .username = std::any_cast<std::string>((*returnedData)[0][0]->data),
                .miiId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][1]->data)),
                .miiName = std::any_cast<std::string>((*returnedData)[0][2]->data),
                .miiData = std::any_cast<std::string>((*returnedData)[0][3]->data),
                .miiPrimary = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][4]->data)),
                .miiHash = std::any_cast<std::string>((*returnedData)[0][5]->data)
            };

            resultsData = std::move(userMii);
        }
    } else if (command->type == DBCommandType::GET_USER_EMAIL) {
        if (getUserEmailStatement == nullptr) {
            std::string sqlCommand = "SELECT e.id, e.address, e.parent, e.`primary`, e.reachable, e.type, "
                                     "e.updated_by, e.validated, e.validated_date, e.validation_code FROM emails e "
                                     "JOIN users u ON u.email_id = e.id WHERE u.pid = ?;";

            if (!craftStatement(sqlCommand, &getUserEmailStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getUserEmailStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
        }

        if (!runStatement(getUserEmailStatement, {DBDataType::INTEGER, DBDataType::STRING,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::STRING, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::DATETIME, DBDataType::STRING}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getUserEmailStatement);
            sqlite3_clear_bindings(getUserEmailStatement);
            goto push_results;
        }

        sqlite3_reset(getUserEmailStatement);
        sqlite3_clear_bindings(getUserEmailStatement);

        if (!returnedData->empty()) {
            DBUserEmail userEmail {
                .emailId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .email = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .emailParent = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][2]->data)),
                .emailPrimary = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][3]->data)),
                .emailReachable = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][4]->data)),
                .emailType = std::any_cast<std::string>((*returnedData)[0][5]->data),
                .emailUpdatedBy = std::any_cast<std::string>((*returnedData)[0][6]->data),
                .emailValidated = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][7]->data)),
                .emailValidatedDate = std::any_cast<datetime_t>((*returnedData)[0][8]->data),
                .emailValidationCode = std::any_cast<std::string>((*returnedData)[0][9]->data)
            };

            resultsData = std::move(userEmail);
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
    } else if (command->type == DBCommandType::GET_ALL_AGREEMENTS) {
        sqlite3_stmt* getAllAgreementsStatement = nullptr;

        std::string sqlCommand = "SELECT type, version, country, language, language_name, publish_date, "
                                 "main_title, sub_title, agree_text, non_agree_text, main_text, sub_text "
                                 "FROM agreements";

        auto* query = std::any_cast<DBAgreementsQuery>(&command->data);

        std::vector<std::shared_ptr<DBData>> bindValues;
        std::vector<DBDataType> bindTypes;

        bool whereAdded = false;
        if (query->type.has_value()) {
            sqlCommand += " WHERE type = ?";
            whereAdded = true;

            bindTypes.push_back(DBDataType::STRING);
            bindValues.push_back(std::make_shared<DBString>(query->type.value()));
        }

        if (query->country.has_value()) {
            sqlCommand += whereAdded ? " AND country = ?" : " WHERE country = ?";
            whereAdded = true;

            bindTypes.push_back(DBDataType::STRING);
            bindValues.push_back(std::make_shared<DBString>(query->country.value()));
        }

        if (query->language.has_value()) {
            sqlCommand += whereAdded ? " AND language = ?" : " WHERE language = ?";
            whereAdded = true;

            bindTypes.push_back(DBDataType::STRING);
            bindValues.push_back(std::make_shared<DBString>(query->language.value()));
        }

        if (query->version.has_value()) {
            sqlCommand += whereAdded ? " AND version = ?" : " WHERE version = ?";

            bindTypes.push_back(DBDataType::INTEGER);
            bindValues.push_back(std::make_shared<DBInteger>(query->version.value()));
        }

        bool orderByAdded = false;
        for (const auto& order : query->sortBy) {
            if (!orderByAdded) {
                sqlCommand += " ORDER BY ";
                orderByAdded = true;
            } else {
                sqlCommand += ", ";
            }

            std::string lowerCaseOrder = order.first;
            std::ranges::transform(lowerCaseOrder, lowerCaseOrder.begin(), ::tolower);

            sqlCommand += lowerCaseOrder + (order.second ? " ASC" : " DESC");
        }

        if (query->pageSize != std::numeric_limits<uint32_t>::max()) {
            sqlCommand += " LIMIT " + std::to_string(query->pageSize) +
                " OFFSET " + std::to_string(static_cast<int64_t>(query->pageSize) *  static_cast<int64_t>(query->pageNumber));
        }

        sqlCommand += ";";

        if (!craftStatement(sqlCommand, &getAllAgreementsStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(getAllAgreementsStatement, bindTypes, bindValues)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getAllAgreementsStatement);
            sqlite3_finalize(getAllAgreementsStatement);
            goto push_results;
        }

        std::vector returnedDataTypes {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                       DBDataType::STRING, DBDataType::DATETIME, DBDataType::STRING, DBDataType::STRING,
                                       DBDataType::STRING, DBDataType::STRING, DBDataType::STRING, DBDataType::STRING};

        if (!runStatement(getAllAgreementsStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getAllAgreementsStatement);
            sqlite3_clear_bindings(getAllAgreementsStatement);
            sqlite3_finalize(getAllAgreementsStatement);
            goto push_results;
        }

        sqlite3_reset(getAllAgreementsStatement);
        sqlite3_clear_bindings(getAllAgreementsStatement);
        sqlite3_finalize(getAllAgreementsStatement);

        std::vector<DBAgreementData> agreementsData;
        for (const auto& row : *returnedData) {
            DBAgreementData agreementData {};
            agreementData.type = std::any_cast<std::string>(row[0]->data);
            agreementData.version = static_cast<int>(std::any_cast<int64_t>(row[1]->data));
            agreementData.country = std::any_cast<std::string>(row[2]->data);
            agreementData.language = std::any_cast<std::string>(row[3]->data);
            agreementData.languageName = std::any_cast<std::string>(row[4]->data);
            agreementData.publishedAt = std::any_cast<datetime_t>(row[5]->data);
            agreementData.mainTitle = std::any_cast<std::string>(row[6]->data);
            agreementData.subTitle = std::any_cast<std::string>(row[7]->data);
            agreementData.agreeText = std::any_cast<std::string>(row[8]->data);
            agreementData.disagreeText = std::any_cast<std::string>(row[9]->data);
            agreementData.mainText = std::any_cast<std::string>(row[10]->data);
            agreementData.subText = std::any_cast<std::string>(row[11]->data);

            agreementsData.push_back(agreementData);
        }

        resultsData = std::move(agreementsData);
    } else if (command->type == DBCommandType::GET_AGREEMENT) {
        auto* query = std::any_cast<DBAgreementQuery>(&command->data);
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
                                     "updated_by, status, banned, last_updated FROM devices WHERE id = ?;";

            if (!craftStatement(sqlCommand, &getDeviceStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBIdQuery>(&command->data);

        if (!bindData(getDeviceStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)}) ) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getDeviceStatement);
            goto push_results;
        }

        if (!runStatement(getDeviceStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                DBDataType::STRING, DBDataType::STRING, DBDataType::STRING,
                                                DBDataType::INTEGER, DBDataType::DATETIME},
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
                .status = std::any_cast<std::string>((*returnedData)[0][8]->data),
                .banned = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][9]->data)),
                .lastUpdated = std::any_cast<datetime_t>((*returnedData)[0][10]->data)
            };

            resultsData = std::move(deviceData);
        }

        sqlite3_reset(getDeviceStatement);
        sqlite3_clear_bindings(getDeviceStatement);
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
            auto count = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data));
            resultsData = count > 0;
        } else {
            resultsData = false; // No active ownerships found
        }

        sqlite3_reset(hasActiveOwnershipStatement);
        sqlite3_clear_bindings(hasActiveOwnershipStatement);
    } else if (command->type == DBCommandType::GET_OWNERSHIPS) {
        if (getOwnershipsStatement == nullptr) {
            std::string sqlCommand = "SELECT pid, device_id, status, last_updated FROM ownerships WHERE pid = ?;";

            if (!craftStatement(sqlCommand, &getOwnershipsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getOwnershipsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getOwnershipsStatement);
            goto push_results;
        }

        if (!runStatement(getOwnershipsStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::STRING, DBDataType::DATETIME}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getOwnershipsStatement);
            sqlite3_clear_bindings(getOwnershipsStatement);
            goto push_results;
        }

        sqlite3_reset(getOwnershipsStatement);
        sqlite3_clear_bindings(getOwnershipsStatement);

        std::vector<DBOwnershipData> ownershipsData;
        for (const auto& row : *returnedData) {
            DBOwnershipData ownershipData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                .status = std::any_cast<std::string>(row[2]->data),
                .lastUpdated = std::any_cast<datetime_t>(row[3]->data)
            };

            ownershipsData.push_back(ownershipData);
        }

        resultsData = ownershipsData;
    } else if (command->type == DBCommandType::GET_BLOCKED_FRIENDS) {
        if (getBlockedFriendsStatement == nullptr) {
            std::string sqlCommand = "SELECT b.pid, b.blocked_pid, b.created_at, b.game_key, u.nna_info "
                                     "FROM blocks AS b LEFT JOIN user_info AS u ON b.blocked_pid = u.pid "
                                     "WHERE b.pid = ?;";

            if (!craftStatement(sqlCommand, &getBlockedFriendsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getBlockedFriendsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getBlockedFriendsStatement);
            goto push_results;
        }

        if (!runStatement(getBlockedFriendsStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                        DBDataType::DATETIME, DBDataType::BLOB, DBDataType::BLOB}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getBlockedFriendsStatement);
            sqlite3_clear_bindings(getBlockedFriendsStatement);
            goto push_results;
        }

        sqlite3_reset(getBlockedFriendsStatement);
        sqlite3_clear_bindings(getBlockedFriendsStatement);

        std::vector<DBBlockData> results;
        for (const auto& row : *returnedData) {
            DBBlockData blockedFriendData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                .blockedPid = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                .createdAt = std::any_cast<datetime_t>(row[2]->data),
                .gameKey = std::any_cast<std::vector<uint8_t>>(row[3]->data),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>(row[4]->data)
            };

            results.push_back(blockedFriendData);
        }

        resultsData = std::move(results);
    } else if (command->type == DBCommandType::GET_PERSISTENT_NOTIFICATIONS) {
        if (getPersistentNotificationsStatement == nullptr) {
            std::string sqlCommand = "SELECT id, for, value1, value2, value3, value4, text FROM notifications "
                                     "WHERE for = ?;";

            if (!craftStatement(sqlCommand, &getPersistentNotificationsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(getPersistentNotificationsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getPersistentNotificationsStatement);
            goto push_results;
        }

        if (!runStatement(getPersistentNotificationsStatement, {DBDataType::INTEGER, DBDataType::INTEGER,
                                                              DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                              DBDataType::INTEGER, DBDataType::STRING}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getPersistentNotificationsStatement);
            sqlite3_clear_bindings(getPersistentNotificationsStatement);
            goto push_results;
        }

        sqlite3_reset(getPersistentNotificationsStatement);
        sqlite3_clear_bindings(getPersistentNotificationsStatement);

        std::vector<DBPersistentNotificationData> notifications;
        for (const auto& row : *returnedData) {
            DBPersistentNotificationData notification {
                .id = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                .forPid = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                .value1 = std::any_cast<int64_t>(row[2]->data),
                .value2 = static_cast<uint32_t>(std::any_cast<int64_t>(row[3]->data)),
                .value3 = static_cast<uint32_t>(std::any_cast<int64_t>(row[4]->data)),
                .value4 = static_cast<uint32_t>(std::any_cast<int64_t>(row[5]->data)),
                .text = std::any_cast<std::string>(row[6]->data)
            };
            notifications.push_back(notification);
        }

        resultsData = notifications;
    } else if (command->type == DBCommandType::INACTIVATE_DEVICE_OWNERSHIPS) {
        if (inactivateDeviceOwnershipsStatement == nullptr) {
            std::string sqlCommand = "UPDATE ownerships SET status = 'INACTIVE', last_updated = ? "
                                     "WHERE device_id = ? AND status = 'ACTIVE';";

            if (!craftStatement(sqlCommand, &inactivateDeviceOwnershipsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBIdQuery>(&command->data);
        datetime_t lastUpdated = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

        if (!bindData(inactivateDeviceOwnershipsStatement, {DBDataType::DATETIME, DBDataType::INTEGER},
                      {std::make_shared<DBDateTime>(lastUpdated),
                       std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(inactivateDeviceOwnershipsStatement);
            goto push_results;
        }

        if (!runStatement(inactivateDeviceOwnershipsStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(inactivateDeviceOwnershipsStatement);
        sqlite3_clear_bindings(inactivateDeviceOwnershipsStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_DEVICE) {
        if (insertOrUpdateDeviceStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO devices (id, language, platform_id, region, serial_num, system_ver, type, updated_by, status, banned, last_updated) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(id) DO UPDATE SET "
                        "language = excluded.language, "
                        "platform_id = excluded.platform_id, "
                        "region = excluded.region, "
                        "serial_num = excluded.serial_num, "
                        "system_ver = excluded.system_ver, "
                        "type = excluded.type, "
                        "updated_by = excluded.updated_by, "
                        "status = excluded.status, "
                        "banned = excluded.banned, "
                        "last_updated = excluded.last_updated;";

            if (!craftStatement(sqlCommand, &insertOrUpdateDeviceStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBDeviceInsertOrUpdateQuery>(&command->data);

        if (!bindData(insertOrUpdateDeviceStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                      DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                      DBDataType::STRING, DBDataType::STRING, DBDataType::STRING,
                                                      DBDataType::INTEGER, DBDataType::DATETIME},
                      {std::make_shared<DBInteger>(query->deviceId),
                       std::make_shared<DBString>(query->language),
                       std::make_shared<DBInteger>(query->platformId),
                       std::make_shared<DBInteger>(query->region),
                       std::make_shared<DBString>(query->serialNumber),
                       std::make_shared<DBString>(query->systemVersion),
                       std::make_shared<DBString>(query->type),
                       std::make_shared<DBString>(query->updatedBy),
                       std::make_shared<DBString>(query->status),
                       std::make_shared<DBInteger>(static_cast<int64_t>(query->banned)),
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
            std::string sqlCommand = "INSERT INTO user_agreements (pid, type, version, country, signed_date) "
                        "VALUES (?, ?, ?, ?, ?) "
                        "ON CONFLICT(pid, type, version, country) DO UPDATE SET "
                        "signed_date = excluded.signed_date;";

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
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_AGREEMENT) {
        if (insertOrUpdateAgreementStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO agreements (type, version, country, language, language_name, publish_date, "
                        "main_title, sub_title, agree_text, non_agree_text, main_text, sub_text) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(type, version, country, language) DO UPDATE SET "
                        "language_name = excluded.language_name, "
                        "publish_date = excluded.publish_date, "
                        "main_title = excluded.main_title, "
                        "sub_title = excluded.sub_title, "
                        "agree_text = excluded.agree_text, "
                        "non_agree_text = excluded.non_agree_text, "
                        "main_text = excluded.main_text, "
                        "sub_text = excluded.sub_text;";

            if (!craftStatement(sqlCommand, &insertOrUpdateAgreementStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBAgreementData>(&command->data);

        if (!bindData(insertOrUpdateAgreementStatement, {DBDataType::STRING, DBDataType::INTEGER,
                                                         DBDataType::STRING, DBDataType::STRING, DBDataType::STRING,
                                                         DBDataType::DATETIME, DBDataType::STRING, DBDataType::STRING,
                                                         DBDataType::STRING, DBDataType::STRING, DBDataType::STRING,
                                                         DBDataType::STRING},
                                                      {std::make_shared<DBString>(query->type),
                                                       std::make_shared<DBInteger>(query->version),
                                                       std::make_shared<DBString>(query->country),
                                                       std::make_shared<DBString>(query->language),
                                                       std::make_shared<DBString>(query->languageName),
                                                       std::make_shared<DBDateTime>(query->publishedAt),
                                                       std::make_shared<DBString>(query->mainTitle),
                                                       std::make_shared<DBString>(query->subTitle),
                                                       std::make_shared<DBString>(query->agreeText),
                                                       std::make_shared<DBString>(query->disagreeText),
                                                       std::make_shared<DBString>(query->mainText),
                                                       std::make_shared<DBString>(query->subText)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateAgreementStatement);
            goto push_results;
        }

        if (!runStatement(insertOrUpdateAgreementStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateAgreementStatement);
            sqlite3_clear_bindings(insertOrUpdateAgreementStatement);
            goto push_results;
        }

        sqlite3_reset(insertOrUpdateAgreementStatement);
        sqlite3_clear_bindings(insertOrUpdateAgreementStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_MII) {
        if (insertOrUpdateMiiStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO miis (id, hash, name, `primary`, data) "
                        "VALUES (?, ?, ?, ?, ?) "
                        "ON CONFLICT(id) DO UPDATE SET "
                        "hash = excluded.hash, "
                        "name = excluded.name, "
                        "`primary` = excluded.`primary`, "
                        "data = excluded.data;";

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
            std::string sqlCommand = "INSERT INTO emails (id, address, parent, `primary`, reachable, type, updated_by, validated, validated_date, validation_code) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(id) DO UPDATE SET "
                        "address = excluded.address, "
                        "parent = excluded.parent, "
                        "`primary` = excluded.`primary`, "
                        "reachable = excluded.reachable, "
                        "type = excluded.type, "
                        "updated_by = excluded.updated_by, "
                        "validated = excluded.validated, "
                        "validated_date = excluded.validated_date, "
                        "validation_code = excluded.validation_code;";

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
                                                     DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME,
                                                     DBDataType::STRING},
                                                      {emailIdData,
                                                       std::make_shared<DBString>(query->email),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->parent)),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->primary)),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->reachable)),
                                                       std::make_shared<DBString>(query->type),
                                                       std::make_shared<DBString>(query->updatedBy),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->validated)),
                                                       std::make_shared<DBDateTime>(query->validatedAt),
                                                       std::make_shared<DBString>(query->validationCode)})) {
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
                                     "VALUES (0, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &insertProfileStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBUserProfileInsertQuery>(&command->data);

        if (!bindData(insertProfileStatement, {DBDataType::STRING, DBDataType::STRING,
                                                 DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                 DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                 DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                 DBDataType::STRING, DBDataType::STRING, DBDataType::DATETIME,
                                                 DBDataType::DATETIME},
                                                  {std::make_shared<DBString>(query->username),
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

        // Use a separate select to get the ID, as triggers break sqlite3_last_insert_rowid (returns 0)
        sqlite3_stmt* getPidByUsernameStatement = nullptr;
        if (!craftStatement("SELECT pid FROM users WHERE username = ?;", &getPidByUsernameStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(getPidByUsernameStatement, {DBDataType::STRING}, {std::make_shared<DBString>(query->username)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(getPidByUsernameStatement);
            goto push_results;
        }

        auto pidResults = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();
        if (!runStatement(getPidByUsernameStatement, {DBDataType::INTEGER}, pidResults) || pidResults->empty() || pidResults->at(0).empty()) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(getPidByUsernameStatement);
            goto push_results;
        }

        int64_t insertedId = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>(pidResults->at(0).at(0))->data);
        sqlite3_finalize(getPidByUsernameStatement);

        if (insertedId <= 0) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        } else {
            resultsData = insertedId;
        }

        sqlite3_reset(insertProfileStatement);
        sqlite3_clear_bindings(insertProfileStatement);
    } else if (command->type == DBCommandType::ADD_FRIEND) {
        if (addFriendStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO friendships (pid, friend_pid, became_friends, uidx_u1, uidx_u2) "
                                     "VALUES (?, ?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &addFriendStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBFriendshipInsertQuery>(&command->data);

        if (!bindData(addFriendStatement, {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::DATETIME,
                                                DBDataType::INTEGER, DBDataType::INTEGER},
                                                {std::make_shared<DBInteger>(query->pid),
                                                 std::make_shared<DBInteger>(query->friendPid),
                                                 std::make_shared<DBDateTime>(query->becameFriends),
                                                 std::make_shared<DBInteger>(query->pid < query->friendPid ? query->pid : query->friendPid),
                                                 std::make_shared<DBInteger>(query->pid < query->friendPid ? query->friendPid : query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(addFriendStatement);
            goto push_results;
        }

        if (!runStatement(addFriendStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(addFriendStatement);
        sqlite3_clear_bindings(addFriendStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_PERSISTENT_NOTIFICATION) {
        if (insertPersistentNotificationStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO notifications (id, for, value1, value2, value3, value4, text) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?) "
                                     "ON CONFLICT(id) DO UPDATE SET "
                                     "for = excluded.for, "
                                     "value1 = excluded.value1, "
                                     "value2 = excluded.value2, "
                                     "value3 = excluded.value3, "
                                     "value4 = excluded.value4, "
                                     "text = excluded.text;";

            if (!craftStatement(sqlCommand, &insertPersistentNotificationStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPersistentNotificationInsertOrUpdateQuery>(&command->data);

        std::shared_ptr<DBData> requestIdData = std::make_shared<DBNull>();
        if (query->id.has_value()) {
            requestIdData = std::make_shared<DBInteger>(query->id.value());
        }

        if (!bindData(insertPersistentNotificationStatement, {requestIdData->type, DBDataType::INTEGER, DBDataType::INTEGER,
                                                              DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING},
                                                      {requestIdData,
                                                       std::make_shared<DBInteger>(query->forPid),
                                                       std::make_shared<DBInteger>(query->value1),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->value2)),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->value3)),
                                                       std::make_shared<DBInteger>(static_cast<int64_t>(query->value4)),
                                                       std::make_shared<DBString>(query->text)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertPersistentNotificationStatement);
            goto push_results;
        }

        if (!runStatement(insertPersistentNotificationStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertPersistentNotificationStatement);
            sqlite3_clear_bindings(insertPersistentNotificationStatement);
            goto push_results;
        }

        int64_t insertedId = query->id.has_value() ? query->id.value() : sqlite3_last_insert_rowid(db);
        if (insertedId < 0) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertPersistentNotificationStatement);
            sqlite3_clear_bindings(insertPersistentNotificationStatement);
            goto push_results;
        }

        resultsData = insertedId;

        sqlite3_reset(insertPersistentNotificationStatement);
        sqlite3_clear_bindings(insertPersistentNotificationStatement);
    } else if (command->type == DBCommandType::BLOCK_FRIEND) {
        if (blockFriendStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO blocks (pid, blocked_pid, created_at, game_key) "
                                     "VALUES (?, ?, ?, ?);";

            if (!craftStatement(sqlCommand, &blockFriendStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBBlockInsertQuery>(&command->data);

        if (!bindData(blockFriendStatement, {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::BLOB},
                                                      {std::make_shared<DBInteger>(query->pid),
                                                       std::make_shared<DBInteger>(query->blockedPid),
                                                       std::make_shared<DBDateTime>(query->createdAt),
                                                       std::make_shared<DBBlob>(query->gameKey)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(blockFriendStatement);
            goto push_results;
        }

        if (!runStatement(blockFriendStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(blockFriendStatement);
        sqlite3_clear_bindings(blockFriendStatement);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_DEVICE_ATTRIBUTES) {
        if (insertOrUpdateDeviceAttributesStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO device_attributes (device_id, pid, name, value, created_date) "
                        "VALUES (?, ?, ?, ?, ?) "
                        "ON CONFLICT(device_id, pid, name) DO UPDATE SET "
                        "value = excluded.value, "
                        "created_date = excluded.created_date;";

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
            std::string sqlCommand = "INSERT INTO ownerships (pid, device_id, status, last_updated) "
                        "VALUES (?, ?, ?, ?) "
                        "ON CONFLICT(pid, device_id) DO UPDATE SET "
                        "status = excluded.status, "
                        "last_updated = excluded.last_updated;";

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
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_FRIEND_REQUEST) {
        if (insertOrUpdateFriendRequestStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO friend_requests (id, from_pid, to_pid, expiration, created_at, data, uidx_u1, uidx_u2) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(id) DO UPDATE SET "
                        "from_pid = excluded.from_pid, "
                        "to_pid = excluded.to_pid, "
                        "expiration = excluded.expiration, "
                        "created_at = excluded.created_at, "
                        "data = excluded.data, "
                        "uidx_u1 = excluded.uidx_u1, "
                        "uidx_u2 = excluded.uidx_u2;";

            if (!craftStatement(sqlCommand, &insertOrUpdateFriendRequestStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBFriendRequestInsertOrUpdateQuery>(&command->data);

        std::shared_ptr<DBData> requestIdData = std::make_shared<DBNull>();
        if (query->id.has_value()) {
            requestIdData = std::make_shared<DBInteger>(query->id.value());
        }

        if (!bindData(insertOrUpdateFriendRequestStatement, {requestIdData->type, DBDataType::INTEGER,
                                                            DBDataType::INTEGER, DBDataType::DATETIME,
                                                            DBDataType::DATETIME, DBDataType::BLOB,
                                                            DBDataType::INTEGER, DBDataType::INTEGER},
                                                      {requestIdData,
                                                       std::make_shared<DBInteger>(query->fromPid),
                                                       std::make_shared<DBInteger>(query->toPid),
                                                       std::make_shared<DBDateTime>(query->expiresAt),
                                                       std::make_shared<DBDateTime>(query->createdAt),
                                                       std::make_shared<DBBlob>(query->data),
                                                       std::make_shared<DBInteger>(query->fromPid < query->toPid ? query->fromPid : query->toPid),
                                                       std::make_shared<DBInteger>(query->fromPid < query->toPid ? query->toPid : query->fromPid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertOrUpdateFriendRequestStatement);
            goto push_results;
        }

        if (!runStatement(insertOrUpdateFriendRequestStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateFriendRequestStatement);
            sqlite3_clear_bindings(insertOrUpdateFriendRequestStatement);
            goto push_results;
        }

        int64_t insertedId = query->id.has_value() ? query->id.value() : sqlite3_last_insert_rowid(db);
        if (insertedId < 0) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertOrUpdateFriendRequestStatement);
            sqlite3_clear_bindings(insertOrUpdateFriendRequestStatement);
            goto push_results;
        }

        resultsData = insertedId;

        sqlite3_reset(insertOrUpdateFriendRequestStatement);
        sqlite3_clear_bindings(insertOrUpdateFriendRequestStatement);
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
    } else if (command->type == DBCommandType::DELETE_USER) {
        if (deleteUserStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM users WHERE pid = ?;";

            if (!craftStatement(sqlCommand, &deleteUserStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteUserStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteUserStatement);
            goto push_results;
        }

        if (!runStatement(deleteUserStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteUserStatement);
        sqlite3_clear_bindings(deleteUserStatement);
    } else if (command->type == DBCommandType::DELETE_USER_OWNERSHIPS) {
        if (deleteUserOwnershipsStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM ownerships WHERE pid = ?;";

            if (!craftStatement(sqlCommand, &deleteUserOwnershipsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteUserOwnershipsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteUserOwnershipsStatement);
            goto push_results;
        }

        if (!runStatement(deleteUserOwnershipsStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteUserOwnershipsStatement);
        sqlite3_clear_bindings(deleteUserOwnershipsStatement);
    } else if (command->type == DBCommandType::DELETE_USER_AGREEMENTS) {
        if (deleteUserAgreementsStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM user_agreements WHERE pid = ?;";

            if (!craftStatement(sqlCommand, &deleteUserAgreementsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteUserAgreementsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteUserAgreementsStatement);
            goto push_results;
        }

        if (!runStatement(deleteUserAgreementsStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteUserAgreementsStatement);
        sqlite3_clear_bindings(deleteUserAgreementsStatement);
    } else if (command->type == DBCommandType::DELETE_USER_DEVICE_ATTRIBUTES) {
        if (deleteUserDeviceAttributesStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM device_attributes WHERE pid = ?;";

            if (!craftStatement(sqlCommand, &deleteUserDeviceAttributesStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteUserDeviceAttributesStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteUserDeviceAttributesStatement);
            goto push_results;
        }

        if (!runStatement(deleteUserDeviceAttributesStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteUserDeviceAttributesStatement);
        sqlite3_clear_bindings(deleteUserDeviceAttributesStatement);
    } else if (command->type == DBCommandType::DELETE_AGREEMENT) {
        auto* query = std::any_cast<DBAgreementQuery>(&command->data);

        // If version is specified, delete only that version; otherwise delete all versions
        if (query->version.has_value()) {
            // Delete specific version
            if (deleteAgreementVersionStatement == nullptr) {
                const std::string sqlCommand = "DELETE FROM agreements WHERE type = ? AND version = ? AND country = ? AND language = ?;";

                if (!craftStatement(sqlCommand, &deleteAgreementVersionStatement)) {
                    resultStatus = DBResultStatus::FAILURE_STMT;
                    goto push_results;
                }
            }

            if (!bindData(deleteAgreementVersionStatement, {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING},
                          {std::make_shared<DBString>(query->type),
                           std::make_shared<DBInteger>(query->version.value()),
                           std::make_shared<DBString>(query->country),
                           std::make_shared<DBString>(query->language)})) {
                resultStatus = DBResultStatus::FAILURE_DATA;
                sqlite3_clear_bindings(deleteAgreementVersionStatement);
                goto push_results;
            }

            if (!runStatement(deleteAgreementVersionStatement, {}, returnedData)) {
                resultStatus = DBResultStatus::FAILURE_EXEC;
            }

            sqlite3_reset(deleteAgreementVersionStatement);
            sqlite3_clear_bindings(deleteAgreementVersionStatement);
        } else {
            // Delete all versions
            if (deleteAgreementStatement == nullptr) {
                std::string sqlCommand = "DELETE FROM agreements WHERE type = ? AND country = ? AND language = ?;";

                if (!craftStatement(sqlCommand, &deleteAgreementStatement)) {
                    resultStatus = DBResultStatus::FAILURE_STMT;
                    goto push_results;
                }
            }

            if (!bindData(deleteAgreementStatement, {DBDataType::STRING, DBDataType::STRING, DBDataType::STRING},
                          {std::make_shared<DBString>(query->type),
                           std::make_shared<DBString>(query->country),
                           std::make_shared<DBString>(query->language)})) {
                resultStatus = DBResultStatus::FAILURE_DATA;
                sqlite3_clear_bindings(deleteAgreementStatement);
                goto push_results;
            }

            if (!runStatement(deleteAgreementStatement, {}, returnedData)) {
                resultStatus = DBResultStatus::FAILURE_EXEC;
            }

            sqlite3_reset(deleteAgreementStatement);
            sqlite3_clear_bindings(deleteAgreementStatement);
        }
    } else if (command->type == DBCommandType::DELETE_FRIEND) {
        if (deleteFriendStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM friendships WHERE pid = ? AND friend_pid = ? OR "
                                     "pid = ? AND friend_pid = ?;";

            if (!craftStatement(sqlCommand, &deleteFriendStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBFriendDeleteQuery>(&command->data);

        if (!bindData(deleteFriendStatement, {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->friendPid),
                      std::make_shared<DBInteger>(query->friendPid), std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteFriendStatement);
            goto push_results;
        }

        if (!runStatement(deleteFriendStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteFriendStatement);
        sqlite3_clear_bindings(deleteFriendStatement);
    } else if (command->type == DBCommandType::DELETE_FRIEND_REQUEST) {
        if (deleteFriendRequestStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM friend_requests WHERE id = ?;";

            if (!craftStatement(sqlCommand, &deleteFriendRequestStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBIdQuery>(&command->data);

        if (!bindData(deleteFriendRequestStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteFriendRequestStatement);
            goto push_results;
        }

        if (!runStatement(deleteFriendRequestStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteFriendRequestStatement);
        sqlite3_clear_bindings(deleteFriendRequestStatement);
    } else if (command->type == DBCommandType::DELETE_PERSISTENT_NOTIFICATION) {
        if (deletePersistentNotificationStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM notifications WHERE id = ?;";

            if (!craftStatement(sqlCommand, &deletePersistentNotificationStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBIdQuery>(&command->data);

        if (!bindData(deletePersistentNotificationStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deletePersistentNotificationStatement);
            goto push_results;
                      }

        if (!runStatement(deletePersistentNotificationStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deletePersistentNotificationStatement);
        sqlite3_clear_bindings(deletePersistentNotificationStatement);
    } else if (command->type == DBCommandType::UNBLOCK_FRIEND) {
        if (unblockFriendStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM blocks WHERE pid = ? AND blocked_pid = ?;";

            if (!craftStatement(sqlCommand, &unblockFriendStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBFriendDeleteQuery>(&command->data);

        if (!bindData(unblockFriendStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->friendPid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(unblockFriendStatement);
            goto push_results;
        }

        if (!runStatement(unblockFriendStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(unblockFriendStatement);
        sqlite3_clear_bindings(unblockFriendStatement);
    } else if (command->type == DBCommandType::COUNT_AGREEMENTS) {
        sqlite3_stmt* countAgreementsStatement = nullptr;

        auto *query = std::any_cast<DBAgreementsQuery>(&command->data);

        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        std::string sqlCommand = "SELECT COUNT(*) FROM agreements";

        bool whereAdded = false;
        if (query->type.has_value()) {
            sqlCommand += " WHERE type = ?";
            whereAdded = true;

            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(query->type.value()));
        }

        if (query->country.has_value()) {
            sqlCommand += whereAdded ? " AND country = ?" : " WHERE country = ?";
            whereAdded = true;

            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(query->country.value()));
        }

        if (query->language.has_value()) {
            sqlCommand += whereAdded ? " AND language = ?" : " WHERE language = ?";
            whereAdded = true;

            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(query->language.value()));
        }

        if (query->version.has_value()) {
            sqlCommand += whereAdded ? " AND version = ?" : " WHERE version = ?";

            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->version.value()));
        }

        sqlCommand += ";";

        if (!craftStatement(sqlCommand, &countAgreementsStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(countAgreementsStatement, dataTypes, data)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(countAgreementsStatement);
            goto push_results;
        }

        if (!runStatement(countAgreementsStatement, {DBDataType::INTEGER}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(countAgreementsStatement);
            goto push_results;
        }

        if (!returnedData->empty() && !(*returnedData)[0].empty()) {
            resultsData = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>((*returnedData)[0][0])->data);
        } else {
            resultsData = static_cast<int64_t>(0);
        }
    } else if (command->type == DBCommandType::GET_SIGNED_AGREEMENTS) {
        sqlite3_stmt* getSignedAgreementsStatement = nullptr;

        auto *query = std::any_cast<DBUserAgreementsQuery>(&command->data);

        std::string sqlCommand = "SELECT type, version, country, signed_date FROM user_agreements WHERE pid = ?;";

        if (!craftStatement(sqlCommand, &getSignedAgreementsStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!bindData(getSignedAgreementsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(getSignedAgreementsStatement);
            goto push_results;
        }

        if (!runStatement(getSignedAgreementsStatement, {DBDataType::STRING, DBDataType::INTEGER,
                                                         DBDataType::STRING, DBDataType::DATETIME},
                          returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(getSignedAgreementsStatement);
            goto push_results;
        }

        std::vector<DBUserAgreementData> agreements;
        for (const auto& row : *returnedData) {
            DBUserAgreementData agreement {
                .type = std::any_cast<std::string>(row[0]->data),
                .version = static_cast<int>(std::any_cast<int64_t>(row[1]->data)),
                .country = std::any_cast<std::string>(row[2]->data),
                .signedAt = std::any_cast<datetime_t>(row[3]->data)
            };
            agreements.push_back(std::move(agreement));
        }

        resultsData = std::move(agreements);
        sqlite3_finalize(getSignedAgreementsStatement);
    } else if (command->type == DBCommandType::LIST_DEVICES) {
        sqlite3_stmt* listDevicesStatement = nullptr;

        auto *query = std::any_cast<DBDevicesListQuery>(&command->data);

        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        std::string sqlCommand = "SELECT id, language, platform_id, region, serial_num, system_ver, type, "
                                 "updated_by, status, banned, last_updated FROM devices";

        bool whereAdded = false;
        if (query->platform.has_value()) {
            sqlCommand += " WHERE platform_id = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->platform.value()));
        }

        if (query->region.has_value()) {
            sqlCommand += whereAdded ? " AND region = ?" : " WHERE region = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->region.value()));
        }

        if (query->banned.has_value()) {
            sqlCommand += whereAdded ? " AND banned = ?" : " WHERE banned = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->banned.value() ? 1 : 0));
        }

        if (query->serialNumber.has_value()) {
            sqlCommand += whereAdded ? " AND serial_num = ?" : " WHERE serial_num = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(query->serialNumber.value()));
        }

        if (query->type.has_value()) {
            sqlCommand += whereAdded ? " AND type = ?" : " WHERE type = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(query->type.value()));
        }

        // Add sorting
        if (!query->sortBy.empty()) {
            sqlCommand += " ORDER BY ";
            bool first = true;
            for (const auto& [field, desc] : query->sortBy) {
                if (!first) sqlCommand += ", ";
                first = false;

                // Map field names to database columns
                if (field == "id") sqlCommand += "id";
                else if (field == "platform") sqlCommand += "platform_id";
                else if (field == "region") sqlCommand += "region";
                else if (field == "serial") sqlCommand += "serial_num";
                else if (field == "lastUpdated") sqlCommand += "last_updated";
                else sqlCommand += field;

                sqlCommand += desc ? " DESC" : " ASC";
            }
        }

        // Add pagination
        if (query->pageSize != std::numeric_limits<uint64_t>::max()) {
            sqlCommand += " LIMIT ? OFFSET ?";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->pageSize));
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->pageNumber * query->pageSize));
        }

        sqlCommand += ";";

        if (!craftStatement(sqlCommand, &listDevicesStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!dataTypes.empty() && !bindData(listDevicesStatement, dataTypes, data)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(listDevicesStatement);
            goto push_results;
        }

        if (!runStatement(listDevicesStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                 DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                 DBDataType::STRING, DBDataType::STRING, DBDataType::STRING,
                                                 DBDataType::INTEGER, DBDataType::DATETIME},
                          returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(listDevicesStatement);
            goto push_results;
        }

        std::vector<DBDeviceData> devices;
        for (const auto& row : *returnedData) {
            DBDeviceData device {
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                .language = std::any_cast<std::string>(row[1]->data),
                .platformId = static_cast<uint32_t>(std::any_cast<int64_t>(row[2]->data)),
                .region = static_cast<uint32_t>(std::any_cast<int64_t>(row[3]->data)),
                .serialNumber = std::any_cast<std::string>(row[4]->data),
                .systemVersion = std::any_cast<std::string>(row[5]->data),
                .type = std::any_cast<std::string>(row[6]->data),
                .updatedBy = std::any_cast<std::string>(row[7]->data),
                .status = std::any_cast<std::string>(row[8]->data),
                .banned = static_cast<bool>(std::any_cast<int64_t>(row[9]->data)),
                .lastUpdated = std::any_cast<datetime_t>(row[10]->data)
            };
            devices.push_back(std::move(device));
        }

        resultsData = std::move(devices);
        sqlite3_finalize(listDevicesStatement);
    } else if (command->type == DBCommandType::LIST_ACCOUNTS) {
        sqlite3_stmt* listAccountsStatement = nullptr;

        auto *query = std::any_cast<DBAccountsListQuery>(&command->data);

        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        std::string sqlCommand = "SELECT u.pid, u.username, u.password, e.id, e.address, e.parent, e.\"primary\", "
                                 "e.reachable, e.type, e.updated_by, e.validated, e.validated_date, e.validation_code, "
                                 "m.id, m.hash, m.name, m.\"primary\", m.data, u.gender, u.region, u.tz, u.language, "
                                 "u.active, u.marketing, u.off_device, u.birth_date, u.country, u.create_date, u.last_updated "
                                 "FROM users u "
                                 "LEFT JOIN emails e ON u.email_id = e.id "
                                 "LEFT JOIN miis m ON u.mii_id = m.id";

        bool whereAdded = false;
        if (query->username.has_value()) {
            sqlCommand += " WHERE u.username LIKE ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>("%" + query->username.value() + "%"));
        }

        if (query->gender.has_value()) {
            sqlCommand += whereAdded ? " AND u.gender = ?" : " WHERE u.gender = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->gender.value() ? 1 : 0));
        }

        if (query->region.has_value()) {
            sqlCommand += whereAdded ? " AND u.region = ?" : " WHERE u.region = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->region.value()));
        }

        if (query->active.has_value()) {
            sqlCommand += whereAdded ? " AND u.active = ?" : " WHERE u.active = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->active.value() ? 1 : 0));
        }

        // Add sorting
        if (!query->sortBy.empty()) {
            sqlCommand += " ORDER BY ";
            bool first = true;
            for (const auto& [field, desc] : query->sortBy) {
                if (!first) sqlCommand += ", ";
                first = false;

                // Map field names to database columns
                if (field == "pid") sqlCommand += "u.pid";
                else if (field == "username") sqlCommand += "u.username";
                else if (field == "region") sqlCommand += "u.region";
                else sqlCommand += "u." + field;

                sqlCommand += desc ? " DESC" : " ASC";
            }
        }

        // Add pagination
        if (query->pageSize != std::numeric_limits<uint64_t>::max()) {
            sqlCommand += " LIMIT ? OFFSET ?";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->pageSize));
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->pageNumber * query->pageSize));
        }

        sqlCommand += ";";

        if (!craftStatement(sqlCommand, &listAccountsStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!dataTypes.empty() && !bindData(listAccountsStatement, dataTypes, data)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(listAccountsStatement);
            goto push_results;
        }

        if (!runStatement(listAccountsStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                  DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                  DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                  DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME,
                                                  DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING,
                                                  DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING,
                                                  DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                  DBDataType::STRING, DBDataType::INTEGER, DBDataType::INTEGER,
                                                  DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                  DBDataType::DATETIME, DBDataType::DATETIME},
                          returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(listAccountsStatement);
            goto push_results;
        }

        std::vector<DBUserProfileData> accounts;
        for (const auto& row : *returnedData) {
            DBUserProfileData account {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                .username = std::any_cast<std::string>(row[1]->data),
                .emailId = std::any_cast<int64_t>(row[3]->data),
                .miiId = std::any_cast<int64_t>(row[13]->data),
                .gender = static_cast<bool>(std::any_cast<int64_t>(row[18]->data)),
                .region = std::any_cast<int64_t>(row[19]->data),
                .tz = std::any_cast<std::string>(row[20]->data),
                .language = std::any_cast<std::string>(row[21]->data),
                .active = static_cast<bool>(std::any_cast<int64_t>(row[22]->data)),
                .marketing = static_cast<bool>(std::any_cast<int64_t>(row[23]->data)),
                .offDevice = static_cast<bool>(std::any_cast<int64_t>(row[24]->data)),
                .birthdate = std::any_cast<std::string>(row[25]->data),
                .country = std::any_cast<std::string>(row[26]->data),
                .created = std::any_cast<datetime_t>(row[27]->data),
                .updated = std::any_cast<datetime_t>(row[28]->data),
                .email = std::any_cast<std::string>(row[4]->data),
                .emailParent = static_cast<bool>(std::any_cast<int64_t>(row[5]->data)),
                .emailPrimary = static_cast<bool>(std::any_cast<int64_t>(row[6]->data)),
                .emailReachable = static_cast<bool>(std::any_cast<int64_t>(row[7]->data)),
                .emailType = std::any_cast<std::string>(row[8]->data),
                .emailUpdatedBy = std::any_cast<std::string>(row[9]->data),
                .emailValidated = static_cast<bool>(std::any_cast<int64_t>(row[10]->data)),
                .emailValidatedDate = std::any_cast<datetime_t>(row[11]->data),
                .emailValidationCode = std::any_cast<std::string>(row[12]->data),
                .miiName = std::any_cast<std::string>(row[15]->data),
                .miiData = std::any_cast<std::string>(row[17]->data),
                .miiPrimary = static_cast<bool>(std::any_cast<int64_t>(row[16]->data)),
                .miiHash = std::any_cast<std::string>(row[14]->data)
            };
            accounts.push_back(std::move(account));
        }

        resultsData = std::move(accounts);
        sqlite3_finalize(listAccountsStatement);
    } else if (command->type == DBCommandType::DELETE_USER_DEVICE_ATTRIBUTE) {
        auto *query = std::any_cast<DBDeviceAttributeDeleteQuery>(&command->data);

        if (deleteAttributeStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM device_attributes WHERE device_id = ? AND pid = ? AND name = ?;";

            if (!craftStatement(sqlCommand, &deleteAttributeStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(deleteAttributeStatement, {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING},
                      {std::make_shared<DBInteger>(query->deviceId),
                       std::make_shared<DBInteger>(query->pid),
                       std::make_shared<DBString>(query->name)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteAttributeStatement);
            goto push_results;
        }

        if (!runStatement(deleteAttributeStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteAttributeStatement);
        sqlite3_clear_bindings(deleteAttributeStatement);
    } else if (command->type == DBCommandType::DELETE_DEVICE_ATTRIBUTES_FOR_OWNERSHIP) {
        auto *query = std::any_cast<DBOwnershipDeleteQuery>(&command->data);

        if (deleteAttributesStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM device_attributes WHERE device_id = ? AND pid = ?;";

            if (!craftStatement(sqlCommand, &deleteAttributesStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(deleteAttributesStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->deviceId),
                       std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteAttributesStatement);
            goto push_results;
        }

        if (!runStatement(deleteAttributesStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteAttributesStatement);
        sqlite3_clear_bindings(deleteAttributesStatement);
    } else if (command->type == DBCommandType::DELETE_DEVICE) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        if (deleteDeviceStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM devices WHERE id = ?;";

            if (!craftStatement(sqlCommand, &deleteDeviceStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(deleteDeviceStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteDeviceStatement);
            goto push_results;
        }

        if (!runStatement(deleteDeviceStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteDeviceStatement);
        sqlite3_clear_bindings(deleteDeviceStatement);
    } else if (command->type == DBCommandType::DELETE_DEVICE_OWNERSHIPS) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        if (deleteDeviceOwnershipsStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM ownerships WHERE device_id = ?;";

            if (!craftStatement(sqlCommand, &deleteDeviceOwnershipsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(deleteDeviceOwnershipsStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteDeviceOwnershipsStatement);
            goto push_results;
        }

        if (!runStatement(deleteDeviceOwnershipsStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteDeviceOwnershipsStatement);
        sqlite3_clear_bindings(deleteDeviceOwnershipsStatement);
    } else if (command->type == DBCommandType::DELETE_DEVICE_ATTRIBUTES) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        if (deleteDeviceAttributesStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM device_attributes WHERE device_id = ?;";

            if (!craftStatement(sqlCommand, &deleteDeviceAttributesStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(deleteDeviceAttributesStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteDeviceAttributesStatement);
            goto push_results;
        }

        if (!runStatement(deleteDeviceAttributesStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteDeviceAttributesStatement);
        sqlite3_clear_bindings(deleteDeviceAttributesStatement);
    } else if (command->type == DBCommandType::DELETE_OWNERSHIP) {
        auto *query = std::any_cast<DBOwnershipDeleteQuery>(&command->data);

        if (deleteOwnershipStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM ownerships WHERE pid = ? AND device_id = ?;";

            if (!craftStatement(sqlCommand, &deleteOwnershipStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(deleteOwnershipStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid),
                       std::make_shared<DBInteger>(query->deviceId)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteOwnershipStatement);
            goto push_results;
        }

        if (!runStatement(deleteOwnershipStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteOwnershipStatement);
        sqlite3_clear_bindings(deleteOwnershipStatement);
    } else if (command->type == DBCommandType::DELETE_USER_AGREEMENT) {
        auto *query = std::any_cast<DBUserAgreementDeleteQuery>(&command->data);

        if (deleteUserAgreementStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM user_agreements WHERE pid = ? AND type = ? AND version = ? AND country = ?;";

            if (!craftStatement(sqlCommand, &deleteUserAgreementStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        if (!bindData(deleteUserAgreementStatement, {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING},
                      {std::make_shared<DBInteger>(query->pid),
                       std::make_shared<DBString>(query->type),
                       std::make_shared<DBInteger>(query->version),
                       std::make_shared<DBString>(query->country)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteUserAgreementStatement);
            goto push_results;
        }

        if (!runStatement(deleteUserAgreementStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteUserAgreementStatement);
        sqlite3_clear_bindings(deleteUserAgreementStatement);
    } else if (command->type == DBCommandType::DELETE_GAME_SERVER_ACCESS) {
        if (deleteGameServerAccessStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM game_server_access WHERE pid = ?;";

            if (!craftStatement(sqlCommand, &deleteGameServerAccessStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteGameServerAccessStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteGameServerAccessStatement);
            goto push_results;
        }

        if (!runStatement(deleteGameServerAccessStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteGameServerAccessStatement);
        sqlite3_clear_bindings(deleteGameServerAccessStatement);
    } else if (command->type == DBCommandType::INACTIVATE_ALL_USER_OWNERSHIPS) {
        auto *query = std::any_cast<DBPidQuery>(&command->data);

        if (inactivateOwnershipsStatement == nullptr) {
            std::string sqlCommand = "UPDATE ownerships SET status = 'INACTIVE', last_updated = ? WHERE pid = ?;";

            if (!craftStatement(sqlCommand, &inactivateOwnershipsStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

        if (!bindData(inactivateOwnershipsStatement, {DBDataType::INTEGER, DBDataType::DATETIME},
                      {std::make_shared<DBInteger>(query->pid), std::make_shared<DBDateTime>(now)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_reset(inactivateOwnershipsStatement);
            sqlite3_clear_bindings(inactivateOwnershipsStatement);
            goto push_results;
        }

        if (!runStatement(inactivateOwnershipsStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(inactivateOwnershipsStatement);
        sqlite3_clear_bindings(inactivateOwnershipsStatement);
    } else if (command->type == DBCommandType::COUNT_DEVICES) {
        sqlite3_stmt* countDevicesStatement = nullptr;

        auto *query = std::any_cast<DBDevicesListQuery>(&command->data);

        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        std::string sqlCommand = "SELECT COUNT(*) FROM devices";

        bool whereAdded = false;
        if (query->platform.has_value()) {
            sqlCommand += " WHERE platform_id = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->platform.value()));
        }

        if (query->region.has_value()) {
            sqlCommand += whereAdded ? " AND region = ?" : " WHERE region = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->region.value()));
        }

        if (query->banned.has_value()) {
            sqlCommand += whereAdded ? " AND banned = ?" : " WHERE banned = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->banned.value() ? 1 : 0));
        }

        if (query->serialNumber.has_value()) {
            sqlCommand += whereAdded ? " AND serial_num = ?" : " WHERE serial_num = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(query->serialNumber.value()));
        }

        if (query->type.has_value()) {
            sqlCommand += whereAdded ? " AND type = ?" : " WHERE type = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>(query->type.value()));
        }

        sqlCommand += ";";

        if (!craftStatement(sqlCommand, &countDevicesStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!dataTypes.empty() && !bindData(countDevicesStatement, dataTypes, data)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(countDevicesStatement);
            goto push_results;
        }

        if (!runStatement(countDevicesStatement, {DBDataType::INTEGER}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(countDevicesStatement);
            goto push_results;
        }

        if (!returnedData->empty() && !(*returnedData)[0].empty()) {
            resultsData = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>((*returnedData)[0][0])->data);
        } else {
            resultsData = static_cast<int64_t>(0);
        }

        sqlite3_finalize(countDevicesStatement);
    } else if (command->type == DBCommandType::COUNT_ACCOUNTS) {
        sqlite3_stmt* countAccountsStatement = nullptr;

        auto *query = std::any_cast<DBAccountsListQuery>(&command->data);

        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        std::string sqlCommand = "SELECT COUNT(*) FROM users u";

        bool whereAdded = false;
        if (query->username.has_value()) {
            sqlCommand += " WHERE u.username LIKE ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::STRING);
            data.emplace_back(std::make_shared<DBString>("%" + query->username.value() + "%"));
        }

        if (query->gender.has_value()) {
            sqlCommand += whereAdded ? " AND u.gender = ?" : " WHERE u.gender = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->gender.value() ? 1 : 0));
        }

        if (query->region.has_value()) {
            sqlCommand += whereAdded ? " AND u.region = ?" : " WHERE u.region = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->region.value()));
        }

        if (query->active.has_value()) {
            sqlCommand += whereAdded ? " AND u.active = ?" : " WHERE u.active = ?";
            whereAdded = true;
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(query->active.value() ? 1 : 0));
        }

        sqlCommand += ";";

        if (!craftStatement(sqlCommand, &countAccountsStatement)) {
            resultStatus = DBResultStatus::FAILURE_STMT;
            goto push_results;
        }

        if (!dataTypes.empty() && !bindData(countAccountsStatement, dataTypes, data)) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_finalize(countAccountsStatement);
            goto push_results;
        }

        if (!runStatement(countAccountsStatement, {DBDataType::INTEGER}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_finalize(countAccountsStatement);
            goto push_results;
        }

        if (!returnedData->empty() && !(*returnedData)[0].empty()) {
            resultsData = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>((*returnedData)[0][0])->data);
        } else {
            resultsData = static_cast<int64_t>(0);
        }

        sqlite3_finalize(countAccountsStatement);
    } else if (command->type == DBCommandType::DELETE_ALL_BLOCKS_BY_PID) {
        if (deleteAllBlocksByPidStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM blocks WHERE pid = ? OR blocked_pid = ?;";
            if (!craftStatement(sqlCommand, &deleteAllBlocksByPidStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteAllBlocksByPidStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteAllBlocksByPidStatement);
            goto push_results;
        }

        if (!runStatement(deleteAllBlocksByPidStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteAllBlocksByPidStatement);
        sqlite3_clear_bindings(deleteAllBlocksByPidStatement);
    } else if (command->type == DBCommandType::DELETE_ALL_FRIEND_REQUESTS_BY_PID) {
        if (deleteAllFriendRequestsByPidStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM friend_requests WHERE from_pid = ? OR to_pid = ?;";
            if (!craftStatement(sqlCommand, &deleteAllFriendRequestsByPidStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteAllFriendRequestsByPidStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteAllFriendRequestsByPidStatement);
            goto push_results;
        }

        if (!runStatement(deleteAllFriendRequestsByPidStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteAllFriendRequestsByPidStatement);
        sqlite3_clear_bindings(deleteAllFriendRequestsByPidStatement);
    } else if (command->type == DBCommandType::DELETE_ALL_FRIENDSHIPS_BY_PID) {
        if (deleteAllFriendshipsByPidStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM friendships WHERE pid = ? OR friend_pid = ?;";
            if (!craftStatement(sqlCommand, &deleteAllFriendshipsByPidStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteAllFriendshipsByPidStatement, {DBDataType::INTEGER, DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteAllFriendshipsByPidStatement);
            goto push_results;
        }

        if (!runStatement(deleteAllFriendshipsByPidStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteAllFriendshipsByPidStatement);
        sqlite3_clear_bindings(deleteAllFriendshipsByPidStatement);
    } else if (command->type == DBCommandType::DELETE_ALL_NOTIFICATIONS_BY_PID) {
        if (deleteAllNotificationsByPidStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM notifications WHERE for = ?;";
            if (!craftStatement(sqlCommand, &deleteAllNotificationsByPidStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteAllNotificationsByPidStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteAllNotificationsByPidStatement);
            goto push_results;
        }

        if (!runStatement(deleteAllNotificationsByPidStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteAllNotificationsByPidStatement);
        sqlite3_clear_bindings(deleteAllNotificationsByPidStatement);
    } else if (command->type == DBCommandType::DELETE_USER_INFO_BY_PID) {
        if (deleteUserInfoByPidStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM user_info WHERE pid = ?;";
            if (!craftStatement(sqlCommand, &deleteUserInfoByPidStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        if (!bindData(deleteUserInfoByPidStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->pid)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteUserInfoByPidStatement);
            goto push_results;
        }

        if (!runStatement(deleteUserInfoByPidStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
        }

        sqlite3_reset(deleteUserInfoByPidStatement);
        sqlite3_clear_bindings(deleteUserInfoByPidStatement);
    } else if (command->type == DBCommandType::INSERT_TASK) {
        if (insertTaskStatement == nullptr) {
            std::string sqlCommand = "INSERT INTO pending_tasks (type, params) VALUES (?, ?);";

            if (!craftStatement(sqlCommand, &insertTaskStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBTaskInsertQuery>(&command->data);

        if (!bindData(insertTaskStatement, {DBDataType::INTEGER, DBDataType::STRING},
                      {std::make_shared<DBInteger>(query->type),
                       std::make_shared<DBString>(query->params)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(insertTaskStatement);
            goto push_results;
        }

        if (!runStatement(insertTaskStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertTaskStatement);
            sqlite3_clear_bindings(insertTaskStatement);
            goto push_results;
        }

        int64_t insertedId = sqlite3_last_insert_rowid(db);
        if (insertedId < 0) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(insertTaskStatement);
            sqlite3_clear_bindings(insertTaskStatement);
            goto push_results;
        }

        resultsData = insertedId;

        sqlite3_reset(insertTaskStatement);
        sqlite3_clear_bindings(insertTaskStatement);
    } else if (command->type == DBCommandType::GET_ALL_TASKS) {
        if (getAllTasksStatement == nullptr) {
            std::string sqlCommand = "SELECT id, type, params FROM pending_tasks;";

            if (!craftStatement(sqlCommand, &getAllTasksStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        std::vector returnedDataTypes {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING};

        if (!runStatement(getAllTasksStatement, returnedDataTypes, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(getAllTasksStatement);
            sqlite3_clear_bindings(getAllTasksStatement);
            goto push_results;
        }

        sqlite3_reset(getAllTasksStatement);
        sqlite3_clear_bindings(getAllTasksStatement);

        std::vector<DBTaskData> tasksData;
        for (const auto& row : *returnedData) {
            DBTaskData taskData {};
            taskData.id = std::any_cast<int64_t>(row[0]->data);
            taskData.type = static_cast<int>(std::any_cast<int64_t>(row[1]->data));
            taskData.params = std::any_cast<std::string>(row[2]->data);

            tasksData.push_back(taskData);
        }

        resultsData = tasksData;
    } else if (command->type == DBCommandType::DELETE_TASK) {
        if (deleteTaskStatement == nullptr) {
            std::string sqlCommand = "DELETE FROM pending_tasks WHERE id = ?;";

            if (!craftStatement(sqlCommand, &deleteTaskStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto *query = std::any_cast<DBIdQuery>(&command->data);

        if (!bindData(deleteTaskStatement, {DBDataType::INTEGER},
                      {std::make_shared<DBInteger>(query->id)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(deleteTaskStatement);
            goto push_results;
        }

        if (!runStatement(deleteTaskStatement, {}, returnedData)) {
            resultStatus = DBResultStatus::FAILURE_EXEC;
            sqlite3_reset(deleteTaskStatement);
            sqlite3_clear_bindings(deleteTaskStatement);
            goto push_results;
        }

        sqlite3_reset(deleteTaskStatement);
        sqlite3_clear_bindings(deleteTaskStatement);
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
    if (getUserInfoByPidStatement != nullptr) sqlite3_finalize(getUserInfoByPidStatement);
    if (getUserInfoByUsernameStatement != nullptr) sqlite3_finalize(getUserInfoByUsernameStatement);
    if (insertUserInfoStatement != nullptr) sqlite3_finalize(insertUserInfoStatement);
    if (getFriendsInfoStatement != nullptr) sqlite3_finalize(getFriendsInfoStatement);
    if (getFriendRequestStatement != nullptr) sqlite3_finalize(getFriendRequestStatement);
    if (getSentFriendRequestsStatement != nullptr) sqlite3_finalize(getSentFriendRequestsStatement);
    if (getReceivedFriendRequestsStatement != nullptr) sqlite3_finalize(getReceivedFriendRequestsStatement);
    if (getBlockedFriendsStatement != nullptr) sqlite3_finalize(getBlockedFriendsStatement);
    if (getUserProfileStatement != nullptr) sqlite3_finalize(getUserProfileStatement);
    if (getUserMiiStatement != nullptr) sqlite3_finalize(getUserMiiStatement);
    if (getUserEmailStatement != nullptr) sqlite3_finalize(getUserEmailStatement);
    if (getDeviceAttributesStatement != nullptr) sqlite3_finalize(getDeviceAttributesStatement);
    if (getAgreementStatement != nullptr) sqlite3_finalize(getAgreementStatement);
    if (getDeviceStatement != nullptr) sqlite3_finalize(getDeviceStatement);
    if (getOwnershipStatement != nullptr) sqlite3_finalize(getOwnershipStatement);
    if (getOwnershipsStatement != nullptr) sqlite3_finalize(getOwnershipsStatement);
    if (getPersistentNotificationsStatement != nullptr) sqlite3_finalize(getPersistentNotificationsStatement);
    if (inactivateDeviceOwnershipsStatement != nullptr) sqlite3_finalize(inactivateDeviceOwnershipsStatement);
    if (getLatestOwnershipStatement != nullptr) sqlite3_finalize(getLatestOwnershipStatement);
    if (getLatestAgreementStatement != nullptr) sqlite3_finalize(getLatestAgreementStatement);
    if (hasActiveOwnershipStatement != nullptr) sqlite3_finalize(hasActiveOwnershipStatement);
    if (addFriendStatement != nullptr) sqlite3_finalize(addFriendStatement);
    if (blockFriendStatement != nullptr) sqlite3_finalize(blockFriendStatement);
    if (insertPersistentNotificationStatement != nullptr) sqlite3_finalize(insertPersistentNotificationStatement);
    if (insertOrUpdateDeviceStatement != nullptr) sqlite3_finalize(insertOrUpdateDeviceStatement);
    if (insertOrUpdateUserAgreementStatement != nullptr) sqlite3_finalize(insertOrUpdateUserAgreementStatement);
    if (insertOrUpdateMiiStatement != nullptr) sqlite3_finalize(insertOrUpdateMiiStatement);
    if (insertOrUpdateEmailStatement != nullptr) sqlite3_finalize(insertOrUpdateEmailStatement);
    if (insertProfileStatement != nullptr) sqlite3_finalize(insertProfileStatement);
    if (insertOrUpdateDeviceAttributesStatement != nullptr) sqlite3_finalize(insertOrUpdateDeviceAttributesStatement);
    if (insertOrUpdateOwnershipStatement != nullptr) sqlite3_finalize(insertOrUpdateOwnershipStatement);
    if (insertOrUpdateFriendRequestStatement != nullptr) sqlite3_finalize(insertOrUpdateFriendRequestStatement);
    if (deleteMiiStatement != nullptr) sqlite3_finalize(deleteMiiStatement);
    if (deleteEmailStatement != nullptr) sqlite3_finalize(deleteEmailStatement);
    if (deleteUserStatement != nullptr) sqlite3_finalize(deleteUserStatement);
    if (deleteUserOwnershipsStatement != nullptr) sqlite3_finalize(deleteUserOwnershipsStatement);
    if (deleteUserAgreementsStatement != nullptr) sqlite3_finalize(deleteUserAgreementsStatement);
    if (deleteUserDeviceAttributesStatement != nullptr) sqlite3_finalize(deleteUserDeviceAttributesStatement);
    if (deleteFriendStatement != nullptr) sqlite3_finalize(deleteFriendStatement);
    if (deleteFriendRequestStatement != nullptr) sqlite3_finalize(deleteFriendRequestStatement);
    if (deletePersistentNotificationStatement != nullptr) sqlite3_finalize(deletePersistentNotificationStatement);
    if (deleteAgreementStatement != nullptr) sqlite3_finalize(deleteAgreementStatement);
    if (deleteAgreementVersionStatement != nullptr) sqlite3_finalize(deleteAgreementVersionStatement);
    if (deleteAttributeStatement != nullptr) sqlite3_finalize(deleteAttributeStatement);
    if (deleteAttributesStatement != nullptr) sqlite3_finalize(deleteAttributesStatement);
    if (deleteDeviceStatement != nullptr) sqlite3_finalize(deleteDeviceStatement);
    if (deleteOwnershipStatement != nullptr) sqlite3_finalize(deleteOwnershipStatement);
    if (deleteUserAgreementStatement != nullptr) sqlite3_finalize(deleteUserAgreementStatement);
    if (deleteGameServerAccessStatement != nullptr) sqlite3_finalize(deleteGameServerAccessStatement);
    if (inactivateOwnershipsStatement != nullptr) sqlite3_finalize(inactivateOwnershipsStatement);
    if (unblockFriendStatement != nullptr) sqlite3_finalize(unblockFriendStatement);
    if (deleteAllBlocksByPidStatement != nullptr) sqlite3_finalize(deleteAllBlocksByPidStatement);
    if (deleteAllFriendRequestsByPidStatement != nullptr) sqlite3_finalize(deleteAllFriendRequestsByPidStatement);
    if (deleteAllFriendshipsByPidStatement != nullptr) sqlite3_finalize(deleteAllFriendshipsByPidStatement);
    if (deleteAllNotificationsByPidStatement != nullptr) sqlite3_finalize(deleteAllNotificationsByPidStatement);
    if (deleteUserInfoByPidStatement != nullptr) sqlite3_finalize(deleteUserInfoByPidStatement);
    if (insertTaskStatement != nullptr) sqlite3_finalize(insertTaskStatement);
    if (getAllTasksStatement != nullptr) sqlite3_finalize(getAllTasksStatement);
    if (deleteTaskStatement != nullptr) sqlite3_finalize(deleteTaskStatement);

    sqlite3_close(db);
    db = nullptr;
}

} // namespace db
