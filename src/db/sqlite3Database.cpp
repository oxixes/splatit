#include <memory>
#include <utility>
#include <chrono>

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
    running = true;
    dbThreadHandle = std::thread(&sqlite3Database::dbThread, this);
    logger->log(Logger::level::INFO, Logger::group::DB, "SQLite 3 database thread started.");
    return true;
}

uint32_t sqlite3Database::queueCommand(std::unique_ptr<Command> command, bool commandMutex) {
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
    dbQueueCV.wait(lock, [this, shouldEnd] {
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
    dbQueueCV.notify_all();
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
        dbQueueCV.notify_all();
        if (!running) break;
    }
}

void sqlite3Database::processCommand(const std::unique_ptr<Command>& command) {
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
            if (!craftStatement("SELECT * FROM game_server_access WHERE pid = ? AND game_server_id = ?;",
                                &getGameServerAccessStatement)) {
                resultStatus = DBResultStatus::FAILURE_STMT;
                goto push_results;
            }
        }

        auto* query = std::any_cast<DBGameServerAccessQuery>(&command->data);

        if (!bindData(getGameServerAccessStatement, {DBDataType::INTEGER, DBDataType::STRING},
                      {std::make_shared<DBInteger>((int64_t) query->pid),
                       std::make_shared<DBString>(query->serverId)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            sqlite3_clear_bindings(getGameServerAccessStatement);
            goto push_results;
        }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING};

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
                .serverId = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .password = std::any_cast<std::string>((*returnedData)[0][2]->data)
            };

            resultsData = std::move(accessData);
        }
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
            std::string sqlCommand = "SELECT fuser.pid "               "AS friend_pid, "
                                            "fuser.username "          "AS friend_username, "
                                            "finfo.show_presence "     "AS show_presence, "
                                            "finfo.show_playing "      "AS show_game, "
                                            "finfo.block_requests "    "AS block_requests, "
                                            "finfo.nna_info "          "AS nna_info, "
                                            "finfo.presence "          "AS presence, "
                                            "finfo.comment "           "AS comment, "
                                            "finfo.last_online "       "AS last_online,"
                                            "friendships.became_friends AS became_friends "
                                            "FROM users "
                                            "JOIN friendships "
                                            "ON users.pid = friendships.pid "
                                            "OR users.pid = friendships.friend_pid "
                                            "JOIN users AS fuser "
                                            "ON (fuser.pid = friendships.pid AND fuser.pid <> users.pid) "
                                            "OR (fuser.pid = friendships.friend_pid AND fuser.pid <> users.pid) "
                                            "JOIN user_info AS finfo "
                                            "ON fuser.pid = finfo.pid "
                                            "WHERE users.pid = ? "
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

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::BLOB,
                                                   DBDataType::BLOB, DBDataType::BLOB, DBDataType::DATETIME,
                                                   DBDataType::DATETIME};

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
                .friendUsername = std::any_cast<std::string>(row[1]->data),
                .showPresence = (bool) std::any_cast<int64_t>(row[2]->data),
                .showPlaying = (bool) std::any_cast<int64_t>(row[3]->data),
                .blockRequests = (bool) std::any_cast<int64_t>(row[4]->data),
                .nnaInfo = std::any_cast<std::vector<uint8_t>>(row[5]->data),
                .presence = std::any_cast<std::vector<uint8_t>>(row[6]->data),
                .comment = std::any_cast<std::vector<uint8_t>>(row[7]->data),
                .lastOnline = std::any_cast<datetime_t>(row[8]->data),
                .becameFriends = std::any_cast<datetime_t>(row[9]->data)
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
        auto& cmdData = command->data;
        if (updateData->showPresence.has_value()) {
            sqlCommand += "show_presence = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>((int64_t) updateData->showPresence.value()));
        }

        if (updateData->showPlaying.has_value()) {
            sqlCommand += "show_playing = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>((int64_t) updateData->showPlaying.value()));
        }

        if (updateData->blockRequests.has_value()) {
            sqlCommand += "block_requests = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>((int64_t) updateData->blockRequests.value()));
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
    } else if (command->type == DBCommandType::GET_USER_PROFILE) {
        if (getUserProfileStatement == nullptr) {
            std::string sqlCommand = "SELECT u.pid, u.username, u.email_id, u.mii_id, u.gender, u.region, u.tz, u.utc_offset,"
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
                                                   DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::STRING, DBDataType::STRING, DBDataType::DATETIME,
                                                   DBDataType::DATETIME, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME,
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
                .pid = (uint32_t) std::any_cast<int64_t>((*returnedData)[0][0]->data),
                .username = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .emailId = std::any_cast<int64_t>((*returnedData)[0][2]->data),
                .miiId = std::any_cast<int64_t>((*returnedData)[0][3]->data),
                .gender = (bool) std::any_cast<int64_t>((*returnedData)[0][4]->data),
                .region = std::any_cast<int64_t>((*returnedData)[0][5]->data),
                .tz = std::any_cast<std::string>((*returnedData)[0][6]->data),
                .utcOffset = (uint32_t) std::any_cast<int64_t>((*returnedData)[0][7]->data),
                .language = std::any_cast<std::string>((*returnedData)[0][8]->data),
                .active = (bool) std::any_cast<int64_t>((*returnedData)[0][9]->data),
                .marketing = (bool) std::any_cast<int64_t>((*returnedData)[0][10]->data),
                .offDevice = (bool) std::any_cast<int64_t>((*returnedData)[0][11]->data),
                .birthdate = std::any_cast<std::string>((*returnedData)[0][12]->data),
                .country = std::any_cast<std::string>((*returnedData)[0][13]->data),
                .created = std::any_cast<datetime_t>((*returnedData)[0][14]->data),
                .updated = std::any_cast<datetime_t>((*returnedData)[0][15]->data),
                .email = std::any_cast<std::string>((*returnedData)[0][16]->data),
                .emailParent = (bool) std::any_cast<int64_t>((*returnedData)[0][17]->data),
                .emailPrimary = (bool) std::any_cast<int64_t>((*returnedData)[0][18]->data),
                .emailReachable = (bool) std::any_cast<int64_t>((*returnedData)[0][19]->data),
                .emailType = std::any_cast<std::string>((*returnedData)[0][20]->data),
                .emailUpdatedBy = std::any_cast<std::string>((*returnedData)[0][21]->data),
                .emailValidated = (bool) std::any_cast<int64_t>((*returnedData)[0][22]->data),
                .emailValidatedDate = std::any_cast<datetime_t>((*returnedData)[0][23]->data),
                .miiName = std::any_cast<std::string>((*returnedData)[0][24]->data),
                .miiData = std::any_cast<std::string>((*returnedData)[0][25]->data),
                .miiPrimary = (bool) std::any_cast<int64_t>((*returnedData)[0][26]->data),
                .miiHash = std::any_cast<std::string>((*returnedData)[0][27]->data)
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
                      {std::make_shared<DBInteger>((int64_t) query->pid),
                       std::make_shared<DBInteger>(query->deviceId)})) {
            resultStatus = DBResultStatus::FAILURE_DATA;
            goto push_results;
        }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
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
                .pid = (uint32_t) std::any_cast<int64_t>(row[0]->data),
                .deviceId = (uint32_t) std::any_cast<int64_t>(row[1]->data),
                .name = std::any_cast<std::string>(row[2]->data),
                .value = std::any_cast<std::string>(row[3]->data),
                .createdDate = std::any_cast<datetime_t>(row[4]->data)
            };

            attributesData.push_back(attributeData);
        }

        resultsData = std::move(attributesData);
    }

    push_results:
    std::unique_lock lock(resultsMutex);
    results.push_back(std::make_unique<Result>(command->commandId, resultStatus, std::move(resultsData)));
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

bool sqlite3Database::bindData(sqlite3_stmt *statement, const std::vector<DBDataType>& dataTypes,
                               const std::vector<std::shared_ptr<DBData>>& data) {
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
                std::time_t tt = std::chrono::system_clock::to_time_t(tp);
                std::tm tm = *std::localtime(&tt);
                std::stringstream ss;
                ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
                std::string date = ss.str();

                result = sqlite3_bind_text(statement, i + 1, date.c_str(), -1, SQLITE_TRANSIENT);
                break;
            }
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
                    case DBDataType::INTEGER:
                        returnedData->back().emplace_back(new DBInteger(sqlite3_column_int64(statement, i)));
                        break;
                    case DBDataType::STRING:
                        returnedData->back().emplace_back(new DBString(std::string(
                                reinterpret_cast<const char *>(sqlite3_column_text(statement, i)))));
                        break;
                    case DBDataType::BLOB: {
                        auto blob = std::vector<uint8_t>();
                        blob.resize(sqlite3_column_bytes(statement, i));
                        memcpy(blob.data(), sqlite3_column_blob(statement, i), blob.size());
                        returnedData->back().emplace_back(new DBBlob(blob));
                        break;
                    }
                    case DBDataType::DATETIME: {
                        std::tm tm{};
                        std::istringstream ss(std::string(
                                reinterpret_cast<const char *>(sqlite3_column_text(statement, i))));
                        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

                        auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                        datetime_t datetime = std::chrono::time_point_cast<std::chrono::seconds>(tp);
                        returnedData->back().emplace_back(new DBDateTime(datetime));
                    }
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
    dbQueueCV.notify_all();

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

    if (getUserByPIDStatement != nullptr) sqlite3_finalize(getUserByPIDStatement);
    if (getUserByUsernameStatement != nullptr) sqlite3_finalize(getUserByUsernameStatement);
    if (getGameServerAccessStatement != nullptr) sqlite3_finalize(getGameServerAccessStatement);
    if (getUserInfoStatement != nullptr) sqlite3_finalize(getUserInfoStatement);
    if (getFriendsInfoStatement != nullptr) sqlite3_finalize(getFriendsInfoStatement);
    if (getUserProfileStatement != nullptr) sqlite3_finalize(getUserProfileStatement);

    sqlite3_close(db);
    db = nullptr;
}

} // namespace db