#include <memory>
#include <utility>
#include <chrono>
#include <algorithm>
#include <limits>

#include "sqlDatabase.hpp"
#include "migrations/migrations.hpp"

namespace db {

sqlDatabase::sqlDatabase(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version) :
        Database(std::move(logger), type, version) {
    dbQueueCV = std::make_shared<std::condition_variable>();
}

sqlDatabase::sqlDatabase(std::shared_ptr<Logger::Logger> logger,
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

    isSession = true; // This is a session database, not the main one
}

DBVersion sqlDatabase::obtainVersion() {
    auto results = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();

    if (execute("SELECT version FROM db_info LIMIT 1;", {}, {}, {DBDataType::STRING}, results, false)
            != DBResultStatus::SUCCESS) {
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

bool sqlDatabase::run() {
    *shouldStop = false;
    dbThreadHandle = std::make_shared<std::thread>(&sqlDatabase::dbThread, this);
    logger->log(Logger::level::INFO, Logger::group::DB, backendName() + " database thread started.");
    return true;
}

async::ManualTask<Result> sqlDatabase::queueCommand(std::unique_ptr<Command> command) {
    if (*shouldStop) return async::ManualTask<Result>();

    auto task = std::make_shared<async::ManualTask<Result>>();

    command->task = task;
    command->db = this->shared_from_this();

    std::unique_lock lock(*commandQueueMutex);
    commandQueue->emplace(std::move(command));

    return *task;
}

async::ManualTask<Result> sqlDatabase::startTransaction(bool immediate) {
    auto command = craftVoidCommand(beginTransactionSQL(immediate));
    auto task = std::move(queueCommand(std::move(command)));

    processQueue();

    return std::move(task);
}

async::ManualTask<Result> sqlDatabase::commitTransaction() {
    auto command = craftVoidCommand("COMMIT TRANSACTION;");
    auto task = std::move(queueCommand(std::move(command)));

    processQueue();

    return std::move(task);
}

async::ManualTask<Result> sqlDatabase::rollbackTransaction() {
    auto command = craftVoidCommand("ROLLBACK TRANSACTION;");
    auto task = std::move(queueCommand(std::move(command)));

    processQueue();

    return std::move(task);
}

void sqlDatabase::processQueue() {
    dbQueueCV->notify_one();
}

void sqlDatabase::waitForQueue() {
    std::unique_lock lock(*queueWaitMutex);
    dbQueueWaitCV->wait(lock, [this] {
        return commandQueue->empty() || *shouldStop;
    });
}

void sqlDatabase::dbThread() const {
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
            std::static_pointer_cast<sqlDatabase>(command->db)->processCommand(command);
            lock.lock();
        }

        dbQueueWaitCV->notify_all();
        if (*shouldStop) break;
    }

    dbQueueWaitCV->notify_all();
}

void sqlDatabase::shutdownQueueThread() {
    if (!isSession) {
        if (dbThreadHandle != nullptr && dbThreadHandle->joinable()) {
            *shouldStop = true;
            dbQueueCV->notify_all();
            dbThreadHandle->join();

            logger->log(Logger::level::INFO, Logger::group::DB, backendName() + " database thread stopped.");
        }

        std::unique_lock queueLock(*commandQueueMutex);
        while (!commandQueue->empty()) {
            commandQueue->pop();
        }
        queueLock.unlock();
    } else {
        logger->log(Logger::level::DEBUG, Logger::group::DB, "Closing " + backendName() + " database session.");
    }
}

void sqlDatabase::processCommand(const std::unique_ptr<Command>& command)
{
    auto returnedData = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();

    std::any resultsData;
    DBResultStatus resultStatus = DBResultStatus::SUCCESS;

    if (!verifyCommandArgs(command)) {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Failed to run " + backendName() + " statement: invalid command arguments");
        resultStatus = DBResultStatus::FAILURE_ARGS;
    } else if (command->type == DBCommandType::GENERIC) {
        auto* query = std::any_cast<DBGenericCommand>(&command->data);

        resultStatus = execute(query->cmd, query->bindTypes, query->bindData, query->resultTypes,
                               returnedData, false);

        if (resultStatus == DBResultStatus::SUCCESS) {
            DBGenericResult result {
                .data = std::move(*returnedData)
            };

            resultsData = std::move(result);
        }
    } else if (command->type == DBCommandType::GET_USER_BY_PID || command->type == DBCommandType::GET_USER_BY_USERNAME) {
        std::string sqlCommand;
        std::shared_ptr<DBData> identifier;
        DBDataType identifierType;
        if (command->type == DBCommandType::GET_USER_BY_PID) {
            sqlCommand = "SELECT pid, username, password, is_admin FROM users WHERE pid = ?;";
            auto* query = std::any_cast<DBPidQuery>(&command->data);
            identifierType = DBDataType::INTEGER;
            identifier = std::make_shared<DBInteger>((int64_t) query->pid);
        } else {
            sqlCommand = "SELECT pid, username, password, is_admin FROM users WHERE username = ?;";
            auto* query = std::any_cast<DBUsernameQuery>(&command->data);
            identifierType = DBDataType::STRING;
            identifier = std::make_shared<DBString>(query->username);
        }

        resultStatus = execute(sqlCommand, {identifierType}, {identifier},
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING, DBDataType::INTEGER},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            DBUserData userData {
                .pid = (uint32_t) std::any_cast<int64_t>((*returnedData)[0][0]->data),
                .username = std::any_cast<std::string>((*returnedData)[0][1]->data),
                .password = std::any_cast<std::string>((*returnedData)[0][2]->data),
                .isAdmin = static_cast<bool>(std::any_cast<int64_t>((*returnedData)[0][3]->data))
            };
            resultsData = std::move(userData);
        }
    } else if (command->type == DBCommandType::GET_GAME_SERVER_ACCESS) {
        auto* query = std::any_cast<DBGameServerAccessQuery>(&command->data);

        resultStatus = execute("SELECT * FROM game_server_access WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>((int64_t) query->pid)},
                               {DBDataType::INTEGER, DBDataType::STRING}, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            DBGameServerAccessData accessData {
                .pid = (uint32_t) std::any_cast<int64_t>((*returnedData)[0][0]->data),
                .password = std::any_cast<std::string>((*returnedData)[0][1]->data)
            };

            resultsData = std::move(accessData);
        }
    } else if (command->type == DBCommandType::INSERT_GAME_SERVER_ACCESS) {
        auto* query = std::any_cast<DBGameServerAccessData>(&command->data);

        resultStatus = execute("INSERT INTO game_server_access (pid, password) VALUES (?, ?);",
                               {DBDataType::INTEGER, DBDataType::STRING},
                               {std::make_shared<DBInteger>((int64_t) query->pid), std::make_shared<DBString>(query->password)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::GET_USER_INFO_BY_PID ||
               command->type == DBCommandType::GET_USER_INFO_BY_USERNAME) {
        std::string sqlCommand;
        std::shared_ptr<DBData> identifier;
        DBDataType identifierType;
        if (command->type == DBCommandType::GET_USER_INFO_BY_PID) {
            sqlCommand = "SELECT pid, username, show_presence, show_playing, block_requests, "
                         "nna_info, presence, comment, last_online FROM user_info WHERE pid = ?;";
            auto* query = std::any_cast<DBPidQuery>(&command->data);
            identifierType = DBDataType::INTEGER;
            identifier = std::make_shared<DBInteger>((int64_t) query->pid);
        } else {
            sqlCommand = "SELECT pid, username, show_presence, show_playing, block_requests, "
                         "nna_info, presence, comment, last_online FROM user_info WHERE username = ?;";
            auto* query = std::any_cast<DBUsernameQuery>(&command->data);
            identifierType = DBDataType::STRING;
            identifier = std::make_shared<DBString>(query->username);
        }

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::BLOB,
                                                   DBDataType::BLOB, DBDataType::BLOB, DBDataType::DATETIME};

        resultStatus = execute(sqlCommand, {identifierType}, {identifier}, returnedDataTypes, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
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

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::BLOB, DBDataType::BLOB,
                                                   DBDataType::BLOB, DBDataType::DATETIME, DBDataType::DATETIME};

        resultStatus = execute(sqlCommand, {DBDataType::INTEGER},
                               {std::make_shared<DBInteger>((int64_t) query->pid)},
                               returnedDataTypes, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
    } else if (command->type == DBCommandType::GET_FRIEND_REQUEST) {
        auto* query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("SELECT id, from_pid, to_pid, expiration, created_at, data"
                               " FROM friend_requests WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                DBDataType::DATETIME, DBDataType::DATETIME, DBDataType::BLOB},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
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
    } else if (command->type == DBCommandType::GET_SENT_FRIEND_REQUESTS ||
               command->type == DBCommandType::GET_RECEIVED_FRIEND_REQUESTS) {
        std::string sqlCommand;
        if (command->type == DBCommandType::GET_SENT_FRIEND_REQUESTS) {
            sqlCommand = "SELECT r.id, r.from_pid, r.to_pid, r.expiration, r.created_at, r.data, u.nna_info "
                         "FROM friend_requests AS r "
                         "JOIN user_info AS u ON r.to_pid = u.pid "
                         "WHERE r.from_pid = ?;";
        } else {
            sqlCommand = "SELECT r.id, r.from_pid, r.to_pid, r.expiration, r.created_at, r.data, u.nna_info "
                         "FROM friend_requests AS r "
                         "JOIN user_info AS u ON r.from_pid = u.pid "
                         "WHERE r.to_pid = ?;";
        }

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute(sqlCommand, {DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                DBDataType::DATETIME, DBDataType::DATETIME, DBDataType::BLOB, DBDataType::BLOB},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
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

        resultStatus = execute(sqlCommand, dataTypes, data, {}, returnedData, false);
    } else if (command->type == DBCommandType::INSERT_USER_INFO) {
        auto* userInfoData = std::any_cast<DBUserInfoData>(&command->data);

        resultStatus = execute("INSERT INTO user_info (pid, username, show_presence, show_playing, block_requests, nna_info, presence, comment, last_online) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
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
                                std::make_shared<DBDateTime>(userInfoData->lastOnline)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::GET_USER_PROFILE) {
        std::string sqlCommand = "SELECT u.pid, u.username, u.email_id, u.mii_id, u.gender, u.region, u.tz,"
                                 "u.language, u.active, u.marketing, u.off_device, u.birth_date, u.country, u.create_date, u.last_updated,"
                                 "e.address AS email_address, e.parent AS email_parent, e.\"primary\" AS email_primary, "
                                 "e.reachable AS email_reachable, e.type AS email_type, e.updated_by AS email_updated_by, "
                                 "e.validated AS email_validated, e.validated_date AS email_validated_date, "
                                 "e.validation_code AS email_validation_code, m.name AS mii_name, "
                                 "m.data AS mii_data, m.\"primary\" AS mii_primary, m.hash AS mii_hash, u.is_admin "
                                 "FROM users u LEFT JOIN emails e ON u.email_id = e.id LEFT JOIN miis m ON u.mii_id = m.id "
                                 "WHERE u.pid = ?;";

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        std::vector<DBDataType> returnedDataTypes {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::STRING, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::DATETIME, DBDataType::DATETIME,
                                                   DBDataType::STRING, DBDataType::INTEGER, DBDataType::INTEGER,
                                                   DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                   DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::STRING, DBDataType::INTEGER,
                                                   DBDataType::STRING, DBDataType::INTEGER};

        resultStatus = execute(sqlCommand, {DBDataType::INTEGER},
                               {std::make_shared<DBInteger>((int64_t) query->pid)},
                               returnedDataTypes, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            const auto& row = (*returnedData)[0];
            DBUserProfileData profileData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                .username = std::any_cast<std::string>(row[1]->data),
                .emailId = row[2]->type == DBDataType::NULL_T ? 0 : std::any_cast<int64_t>(row[2]->data),
                .miiId = row[3]->type == DBDataType::NULL_T ? 0 : std::any_cast<int64_t>(row[3]->data),
                .gender = static_cast<bool>(std::any_cast<int64_t>(row[4]->data)),
                .region = std::any_cast<int64_t>(row[5]->data),
                .tz = std::any_cast<std::string>(row[6]->data),
                .language = std::any_cast<std::string>(row[7]->data),
                .active = static_cast<bool>(std::any_cast<int64_t>(row[8]->data)),
                .marketing = static_cast<bool>(std::any_cast<int64_t>(row[9]->data)),
                .offDevice = static_cast<bool>(std::any_cast<int64_t>(row[10]->data)),
                .birthdate = std::any_cast<std::string>(row[11]->data),
                .country = std::any_cast<std::string>(row[12]->data),
                .created = std::any_cast<datetime_t>(row[13]->data),
                .updated = std::any_cast<datetime_t>(row[14]->data),
                .email = row[15]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[15]->data),
                .emailParent = row[16]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[16]->data)),
                .emailPrimary = row[17]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[17]->data)),
                .emailReachable = row[18]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[18]->data)),
                .emailType = row[19]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[19]->data),
                .emailUpdatedBy = row[20]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[20]->data),
                .emailValidated = row[21]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[21]->data)),
                .emailValidatedDate = row[22]->type == DBDataType::NULL_T ? datetime_t{} : std::any_cast<datetime_t>(row[22]->data),
                .emailValidationCode = row[23]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[23]->data),
                .miiName = row[24]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[24]->data),
                .miiData = row[25]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[25]->data),
                .miiPrimary = row[26]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[26]->data)),
                .miiHash = row[27]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[27]->data),
                .isAdmin = static_cast<bool>(std::any_cast<int64_t>(row[28]->data))
            };

            resultsData = std::move(profileData);
        }
    } else if (command->type == DBCommandType::GET_USER_MII) {
        std::string sqlCommand = "SELECT u.username, u.mii_id, m.name AS mii_name, m.data AS mii_data, "
                                 "m.\"primary\" AS mii_primary, m.hash AS mii_hash "
                                 "FROM users u LEFT JOIN miis m ON u.mii_id = m.id WHERE u.pid = ?;";

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute(sqlCommand, {DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))},
                               {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            const auto& row = (*returnedData)[0];
            DBUserMii userMii {
                .username = std::any_cast<std::string>(row[0]->data),
                .miiId = row[1]->type == DBDataType::NULL_T ? 0 : static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                .miiName = row[2]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[2]->data),
                .miiData = row[3]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[3]->data),
                .miiPrimary = row[4]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[4]->data)),
                .miiHash = row[5]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[5]->data)
            };

            resultsData = std::move(userMii);
        }
    } else if (command->type == DBCommandType::GET_USER_EMAIL) {
        std::string sqlCommand = "SELECT e.id, e.address, e.parent, e.\"primary\", e.reachable, e.type, "
                                 "e.updated_by, e.validated, e.validated_date, e.validation_code FROM emails e "
                                 "JOIN users u ON u.email_id = e.id WHERE u.pid = ?;";

        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute(sqlCommand, {DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))},
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::STRING},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
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
        auto* query = std::any_cast<DBDeviceAttributesQuery>(&command->data);

        resultStatus = execute("SELECT * FROM device_attributes WHERE pid = ? AND device_id = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(static_cast<int64_t>(query->pid)),
                                std::make_shared<DBInteger>(query->deviceId)},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::DATETIME},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
    } else if (command->type == DBCommandType::GET_ALL_AGREEMENTS) {
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

        if (query->pageSize != std::numeric_limits<uint64_t>::max()) {
            sqlCommand += " LIMIT " + std::to_string(query->pageSize) +
                " OFFSET " + std::to_string(static_cast<int64_t>(query->pageSize) *  static_cast<int64_t>(query->pageNumber));
        }

        sqlCommand += ";";

        std::vector returnedDataTypes {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                       DBDataType::STRING, DBDataType::DATETIME, DBDataType::STRING, DBDataType::STRING,
                                       DBDataType::STRING, DBDataType::STRING, DBDataType::STRING, DBDataType::STRING};

        resultStatus = execute(sqlCommand, bindTypes, bindValues, returnedDataTypes, returnedData, false);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
    } else if (command->type == DBCommandType::GET_AGREEMENT) {
        auto* query = std::any_cast<DBAgreementQuery>(&command->data);

        std::vector<DBDataType> returnedDataTypes {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::DATETIME, DBDataType::STRING, DBDataType::STRING,
                                                   DBDataType::STRING, DBDataType::STRING, DBDataType::STRING, DBDataType::STRING};

        if (query->version.has_value()) {
            const std::string sqlCommand = "SELECT type, version, country, language, language_name, publish_date, "
                                           "main_title, sub_title, agree_text, non_agree_text, main_text, sub_text "
                                           "FROM agreements WHERE type = ? AND version = ? AND country = ? AND language = ?;";

            resultStatus = execute(sqlCommand,
                                   {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING},
                                   {std::make_shared<DBString>(query->type),
                                    std::make_shared<DBInteger>(query->version.value()),
                                    std::make_shared<DBString>(query->country),
                                    std::make_shared<DBString>(query->language)},
                                   returnedDataTypes, returnedData, true);
        } else {
            const std::string sqlCommand = "SELECT type, version, country, language, language_name, publish_date, "
                                           "main_title, sub_title, agree_text, non_agree_text, main_text, sub_text "
                                           "FROM agreements WHERE type = ? AND country = ? AND language = ? "
                                           "ORDER BY version DESC LIMIT 1;";

            resultStatus = execute(sqlCommand,
                                   {DBDataType::STRING, DBDataType::STRING, DBDataType::STRING},
                                   {std::make_shared<DBString>(query->type),
                                    std::make_shared<DBString>(query->country),
                                    std::make_shared<DBString>(query->language)},
                                   returnedDataTypes, returnedData, true);
        }

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
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
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("SELECT id, language, platform_id, region, serial_num, system_ver, type, "
                               "updated_by, status, banned, last_updated FROM devices WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::STRING, DBDataType::STRING,
                                DBDataType::INTEGER, DBDataType::DATETIME},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
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
    } else if (command->type == DBCommandType::GET_OWNERSHIP) {
        auto *query = std::any_cast<DBOwnershipQuery>(&command->data);

        resultStatus = execute("SELECT pid, device_id, status, last_updated FROM ownerships WHERE pid = ? "
                               "AND device_id = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->deviceId)},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING, DBDataType::DATETIME},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            DBOwnershipData ownershipData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][1]->data)),
                .status = std::any_cast<std::string>((*returnedData)[0][2]->data),
                .lastUpdated = std::any_cast<datetime_t>((*returnedData)[0][3]->data)
            };

            resultsData = std::move(ownershipData);
        }
    } else if (command->type == DBCommandType::GET_LATEST_OWNERSHIP) {
        auto *query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("SELECT pid, device_id, status, last_updated FROM ownerships "
                               "WHERE pid = ? ORDER BY last_updated DESC LIMIT 1;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING, DBDataType::DATETIME},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            DBOwnershipData ownershipData {
                .pid = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data)),
                .deviceId = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][1]->data)),
                .status = std::any_cast<std::string>((*returnedData)[0][2]->data),
                .lastUpdated = std::any_cast<datetime_t>((*returnedData)[0][3]->data)
            };

            resultsData = std::move(ownershipData);
        }
    } else if (command->type == DBCommandType::HAS_ACTIVE_OWNERSHIP) {
        auto *query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("SELECT COUNT(*) FROM ownerships WHERE pid = ? AND status = 'ACTIVE';",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {DBDataType::INTEGER}, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
            if (!returnedData->empty()) {
                // The result is a count of active ownerships, so we can just return the first value
                auto count = static_cast<uint32_t>(std::any_cast<int64_t>((*returnedData)[0][0]->data));
                resultsData = count > 0;
            } else {
                resultsData = false; // No active ownerships found
            }
        }
    } else if (command->type == DBCommandType::GET_OWNERSHIPS) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("SELECT pid, device_id, status, last_updated FROM ownerships WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING, DBDataType::DATETIME},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
    } else if (command->type == DBCommandType::GET_BLOCKED_FRIENDS) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("SELECT b.pid, b.blocked_pid, b.created_at, b.game_key, u.nna_info "
                               "FROM blocks AS b LEFT JOIN user_info AS u ON b.blocked_pid = u.pid "
                               "WHERE b.pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::DATETIME,
                                DBDataType::BLOB, DBDataType::BLOB},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
            std::vector<DBBlockData> results;
            for (const auto& row : *returnedData) {
                DBBlockData blockedFriendData {
                    .pid = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                    .blockedPid = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                    .createdAt = std::any_cast<datetime_t>(row[2]->data),
                    .gameKey = std::any_cast<std::vector<uint8_t>>(row[3]->data),
                    .nnaInfo = row[4]->type == DBDataType::NULL_T ? std::vector<uint8_t>{} : std::any_cast<std::vector<uint8_t>>(row[4]->data)
                };

                results.push_back(blockedFriendData);
            }

            resultsData = std::move(results);
        }
    } else if (command->type == DBCommandType::GET_PERSISTENT_NOTIFICATIONS) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("SELECT id, \"for\", value1, value2, value3, value4, text FROM notifications "
                               "WHERE \"for\" = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
    } else if (command->type == DBCommandType::GET_SETTING) {
        auto* query = std::any_cast<DBGetSettingQuery>(&command->data);

        resultStatus = execute("SELECT value FROM settings WHERE key = ?;",
                               {DBDataType::STRING}, {std::make_shared<DBString>(query->key)},
                               {DBDataType::STRING}, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            resultsData = std::any_cast<std::string>((*returnedData)[0][0]->data);
        }
    } else if (command->type == DBCommandType::GET_FILE) {
        auto* query = std::any_cast<DBGetFileQuery>(&command->data);

        resultStatus = execute("SELECT data FROM files WHERE hash = ?;",
                               {DBDataType::STRING}, {std::make_shared<DBString>(query->hash)},
                               {DBDataType::BLOB}, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS && !returnedData->empty()) {
            resultsData = std::any_cast<std::vector<uint8_t>>((*returnedData)[0][0]->data);
        }
    } else if (command->type == DBCommandType::INACTIVATE_DEVICE_OWNERSHIPS) {
        auto* query = std::any_cast<DBIdQuery>(&command->data);
        datetime_t lastUpdated = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

        resultStatus = execute("UPDATE ownerships SET status = 'INACTIVE', last_updated = ? "
                               "WHERE device_id = ? AND status = 'ACTIVE';",
                               {DBDataType::DATETIME, DBDataType::INTEGER},
                               {std::make_shared<DBDateTime>(lastUpdated),
                                std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_DEVICE) {
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

        auto *query = std::any_cast<DBDeviceInsertOrUpdateQuery>(&command->data);

        resultStatus = execute(sqlCommand,
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
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
                                std::make_shared<DBDateTime>(query->lastUpdated)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_USER_AGREEMENT) {
        std::string sqlCommand = "INSERT INTO user_agreements (pid, type, version, country, signed_date) "
                    "VALUES (?, ?, ?, ?, ?) "
                    "ON CONFLICT(pid, type, version, country) DO UPDATE SET "
                    "signed_date = excluded.signed_date;";

        auto *query = std::any_cast<DBUserAgreementInsertOrUpdateQuery>(&command->data);

        resultStatus = execute(sqlCommand,
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                DBDataType::STRING, DBDataType::DATETIME},
                               {std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBString>(query->type),
                                std::make_shared<DBInteger>(query->version),
                                std::make_shared<DBString>(query->country),
                                std::make_shared<DBDateTime>(query->signedAt)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_AGREEMENT) {
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

        auto *query = std::any_cast<DBAgreementData>(&command->data);

        resultStatus = execute(sqlCommand,
                               {DBDataType::STRING, DBDataType::INTEGER,
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
                                std::make_shared<DBString>(query->subText)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_MII) {
        auto *query = std::any_cast<DBMiiInsertOrUpdateQuery>(&command->data);

        int64_t insertedId = -1;
        if (query->miiId.has_value()) {
            std::string sqlCommand = "INSERT INTO miis (id, hash, name, \"primary\", data) "
                        "VALUES (?, ?, ?, ?, ?) "
                        "ON CONFLICT(id) DO UPDATE SET "
                        "hash = excluded.hash, "
                        "name = excluded.name, "
                        "\"primary\" = excluded.\"primary\", "
                        "data = excluded.data;";

            resultStatus = execute(sqlCommand,
                                   {DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                    DBDataType::INTEGER, DBDataType::STRING},
                                   {std::make_shared<DBInteger>(query->miiId.value()),
                                    std::make_shared<DBString>(query->hash),
                                    std::make_shared<DBString>(query->name),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->primary)),
                                    std::make_shared<DBString>(query->data)},
                                   {}, returnedData, true);

            insertedId = query->miiId.value();
        } else {
            resultStatus = executeInsertReturningId("INSERT INTO miis (hash, name, \"primary\", data) "
                                                    "VALUES (?, ?, ?, ?);",
                                                    {DBDataType::STRING, DBDataType::STRING,
                                                     DBDataType::INTEGER, DBDataType::STRING},
                                                    {std::make_shared<DBString>(query->hash),
                                                     std::make_shared<DBString>(query->name),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->primary)),
                                                     std::make_shared<DBString>(query->data)},
                                                    insertedId);
        }

        if (resultStatus == DBResultStatus::SUCCESS) {
            resultsData = insertedId;
        }
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_EMAIL) {
        auto *query = std::any_cast<DBEmailInsertOrUpdateQuery>(&command->data);

        int64_t insertedId = -1;
        if (query->emailId.has_value()) {
            std::string sqlCommand = "INSERT INTO emails (id, address, parent, \"primary\", reachable, type, updated_by, validated, validated_date, validation_code) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(id) DO UPDATE SET "
                        "address = excluded.address, "
                        "parent = excluded.parent, "
                        "\"primary\" = excluded.\"primary\", "
                        "reachable = excluded.reachable, "
                        "type = excluded.type, "
                        "updated_by = excluded.updated_by, "
                        "validated = excluded.validated, "
                        "validated_date = excluded.validated_date, "
                        "validation_code = excluded.validation_code;";

            resultStatus = execute(sqlCommand,
                                   {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                    DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                    DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME,
                                    DBDataType::STRING},
                                   {std::make_shared<DBInteger>(query->emailId.value()),
                                    std::make_shared<DBString>(query->email),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->parent)),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->primary)),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->reachable)),
                                    std::make_shared<DBString>(query->type),
                                    std::make_shared<DBString>(query->updatedBy),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->validated)),
                                    std::make_shared<DBDateTime>(query->validatedAt),
                                    std::make_shared<DBString>(query->validationCode)},
                                   {}, returnedData, true);

            insertedId = query->emailId.value();
        } else {
            resultStatus = executeInsertReturningId("INSERT INTO emails (address, parent, \"primary\", reachable, type, updated_by, validated, validated_date, validation_code) "
                                                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
                                                    {DBDataType::STRING, DBDataType::INTEGER,
                                                     DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                                     DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME,
                                                     DBDataType::STRING},
                                                    {std::make_shared<DBString>(query->email),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->parent)),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->primary)),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->reachable)),
                                                     std::make_shared<DBString>(query->type),
                                                     std::make_shared<DBString>(query->updatedBy),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->validated)),
                                                     std::make_shared<DBDateTime>(query->validatedAt),
                                                     std::make_shared<DBString>(query->validationCode)},
                                                    insertedId);
        }

        if (resultStatus == DBResultStatus::SUCCESS) {
            resultsData = insertedId;
        }
    } else if (command->type == DBCommandType::INSERT_USER_PROFILE) {
        std::string sqlCommand = "INSERT INTO users (pid, username, password, email_id, mii_id, gender, region, tz, "
                                 "language, active, marketing, off_device, birth_date, country, create_date, last_updated) "
                                 "VALUES (0, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

        auto *query = std::any_cast<DBUserProfileInsertQuery>(&command->data);

        resultStatus = execute(sqlCommand,
                               {DBDataType::STRING, DBDataType::STRING,
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
                                std::make_shared<DBDateTime>(query->updated)},
                               {}, returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
            // Use a separate select to get the ID, as the pid is assigned by a database trigger
            // (which breaks last-insert-id mechanisms)
            auto pidResults = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();

            resultStatus = execute("SELECT pid FROM users WHERE username = ?;",
                                   {DBDataType::STRING}, {std::make_shared<DBString>(query->username)},
                                   {DBDataType::INTEGER}, pidResults, true);

            if (resultStatus == DBResultStatus::SUCCESS) {
                if (pidResults->empty() || pidResults->at(0).empty()) {
                    resultStatus = DBResultStatus::FAILURE_EXEC;
                } else {
                    int64_t insertedId = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>(pidResults->at(0).at(0))->data);
                    if (insertedId <= 0) {
                        resultStatus = DBResultStatus::FAILURE_EXEC;
                    } else {
                        resultsData = insertedId;
                    }
                }
            }
        }
    } else if (command->type == DBCommandType::ADD_FRIEND) {
        auto* query = std::any_cast<DBFriendshipInsertQuery>(&command->data);

        resultStatus = execute("INSERT INTO friendships (pid, friend_pid, became_friends, uidx_u1, uidx_u2) "
                               "VALUES (?, ?, ?, ?, ?);",
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::DATETIME,
                                DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBInteger>(query->friendPid),
                                std::make_shared<DBDateTime>(query->becameFriends),
                                std::make_shared<DBInteger>(query->pid < query->friendPid ? query->pid : query->friendPid),
                                std::make_shared<DBInteger>(query->pid < query->friendPid ? query->friendPid : query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_PERSISTENT_NOTIFICATION) {
        auto* query = std::any_cast<DBPersistentNotificationInsertOrUpdateQuery>(&command->data);

        int64_t insertedId = -1;
        if (query->id.has_value()) {
            std::string sqlCommand = "INSERT INTO notifications (id, \"for\", value1, value2, value3, value4, text) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?) "
                                     "ON CONFLICT(id) DO UPDATE SET "
                                     "\"for\" = excluded.\"for\", "
                                     "value1 = excluded.value1, "
                                     "value2 = excluded.value2, "
                                     "value3 = excluded.value3, "
                                     "value4 = excluded.value4, "
                                     "text = excluded.text;";

            resultStatus = execute(sqlCommand,
                                   {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                    DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING},
                                   {std::make_shared<DBInteger>(query->id.value()),
                                    std::make_shared<DBInteger>(query->forPid),
                                    std::make_shared<DBInteger>(query->value1),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->value2)),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->value3)),
                                    std::make_shared<DBInteger>(static_cast<int64_t>(query->value4)),
                                    std::make_shared<DBString>(query->text)},
                                   {}, returnedData, true);

            insertedId = query->id.value();
        } else {
            resultStatus = executeInsertReturningId("INSERT INTO notifications (\"for\", value1, value2, value3, value4, text) "
                                                    "VALUES (?, ?, ?, ?, ?, ?);",
                                                    {DBDataType::INTEGER, DBDataType::INTEGER,
                                                     DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER,
                                                     DBDataType::STRING},
                                                    {std::make_shared<DBInteger>(query->forPid),
                                                     std::make_shared<DBInteger>(query->value1),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->value2)),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->value3)),
                                                     std::make_shared<DBInteger>(static_cast<int64_t>(query->value4)),
                                                     std::make_shared<DBString>(query->text)},
                                                    insertedId);
        }

        if (resultStatus == DBResultStatus::SUCCESS) {
            resultsData = insertedId;
        }
    } else if (command->type == DBCommandType::BLOCK_FRIEND) {
        auto* query = std::any_cast<DBBlockInsertQuery>(&command->data);

        resultStatus = execute("INSERT INTO blocks (pid, blocked_pid, created_at, game_key) "
                               "VALUES (?, ?, ?, ?);",
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::DATETIME, DBDataType::BLOB},
                               {std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBInteger>(query->blockedPid),
                                std::make_shared<DBDateTime>(query->createdAt),
                                std::make_shared<DBBlob>(query->gameKey)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_DEVICE_ATTRIBUTES) {
        std::string sqlCommand = "INSERT INTO device_attributes (device_id, pid, name, value, created_date) "
                    "VALUES (?, ?, ?, ?, ?) "
                    "ON CONFLICT(device_id, pid, name) DO UPDATE SET "
                    "value = excluded.value, "
                    "created_date = excluded.created_date;";

        auto *query = std::any_cast<DBDeviceAttributesInsertOrUpdateQuery>(&command->data);

        resultStatus = execute(sqlCommand,
                               {DBDataType::INTEGER, DBDataType::INTEGER,
                                DBDataType::STRING, DBDataType::STRING, DBDataType::DATETIME},
                               {std::make_shared<DBInteger>(query->deviceId),
                                std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBString>(query->name),
                                std::make_shared<DBString>(query->value),
                                std::make_shared<DBDateTime>(query->createdDate)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_OWNERSHIP) {
        std::string sqlCommand = "INSERT INTO ownerships (pid, device_id, status, last_updated) "
                    "VALUES (?, ?, ?, ?) "
                    "ON CONFLICT(pid, device_id) DO UPDATE SET "
                    "status = excluded.status, "
                    "last_updated = excluded.last_updated;";

        auto *query = std::any_cast<DBOwnershipInsertOrUpdateQuery>(&command->data);

        resultStatus = execute(sqlCommand,
                               {DBDataType::INTEGER, DBDataType::INTEGER,
                                DBDataType::STRING, DBDataType::DATETIME},
                               {std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBInteger>(query->deviceId),
                                std::make_shared<DBString>(query->status),
                                std::make_shared<DBDateTime>(query->lastUpdated)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_FRIEND_REQUEST) {
        auto* query = std::any_cast<DBFriendRequestInsertOrUpdateQuery>(&command->data);

        int64_t insertedId = -1;
        if (query->id.has_value()) {
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

            resultStatus = execute(sqlCommand,
                                   {DBDataType::INTEGER, DBDataType::INTEGER,
                                    DBDataType::INTEGER, DBDataType::DATETIME,
                                    DBDataType::DATETIME, DBDataType::BLOB,
                                    DBDataType::INTEGER, DBDataType::INTEGER},
                                   {std::make_shared<DBInteger>(query->id.value()),
                                    std::make_shared<DBInteger>(query->fromPid),
                                    std::make_shared<DBInteger>(query->toPid),
                                    std::make_shared<DBDateTime>(query->expiresAt),
                                    std::make_shared<DBDateTime>(query->createdAt),
                                    std::make_shared<DBBlob>(query->data),
                                    std::make_shared<DBInteger>(query->fromPid < query->toPid ? query->fromPid : query->toPid),
                                    std::make_shared<DBInteger>(query->fromPid < query->toPid ? query->toPid : query->fromPid)},
                                   {}, returnedData, true);

            insertedId = query->id.value();
        } else {
            resultStatus = executeInsertReturningId("INSERT INTO friend_requests (from_pid, to_pid, expiration, created_at, data, uidx_u1, uidx_u2) "
                                                    "VALUES (?, ?, ?, ?, ?, ?, ?);",
                                                    {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::DATETIME,
                                                     DBDataType::DATETIME, DBDataType::BLOB,
                                                     DBDataType::INTEGER, DBDataType::INTEGER},
                                                    {std::make_shared<DBInteger>(query->fromPid),
                                                     std::make_shared<DBInteger>(query->toPid),
                                                     std::make_shared<DBDateTime>(query->expiresAt),
                                                     std::make_shared<DBDateTime>(query->createdAt),
                                                     std::make_shared<DBBlob>(query->data),
                                                     std::make_shared<DBInteger>(query->fromPid < query->toPid ? query->fromPid : query->toPid),
                                                     std::make_shared<DBInteger>(query->fromPid < query->toPid ? query->toPid : query->fromPid)},
                                                    insertedId);
        }

        if (resultStatus == DBResultStatus::SUCCESS) {
            resultsData = insertedId;
        }
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_SETTING) {
        auto* query = std::any_cast<DBInsertOrUpdateSettingQuery>(&command->data);

        resultStatus = execute("INSERT INTO settings (key, value) "
                               "VALUES (?, ?) "
                               "ON CONFLICT(key) DO UPDATE SET "
                               "value = excluded.value;",
                               {DBDataType::STRING, DBDataType::STRING},
                               {std::make_shared<DBString>(query->key),
                                std::make_shared<DBString>(query->value)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_OR_UPDATE_FILE) {
        auto* query = std::any_cast<DBInsertOrUpdateFileQuery>(&command->data);

        resultStatus = execute("INSERT INTO files (hash, data) "
                               "VALUES (?, ?) "
                               "ON CONFLICT(hash) DO UPDATE SET "
                               "data = excluded.data;",
                               {DBDataType::STRING, DBDataType::BLOB},
                               {std::make_shared<DBString>(query->hash),
                                std::make_shared<DBBlob>(query->data)},
                               {}, returnedData, true);
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

        if (updateData->isAdmin.has_value()) {
            sqlCommand += "is_admin = ?, ";
            dataTypes.push_back(DBDataType::INTEGER);
            data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->isAdmin.value())));
        }

        // Remove the last comma and space and add the WHERE clause
        sqlCommand = sqlCommand.substr(0, sqlCommand.size() - 2) + " WHERE pid = ?;";

        dataTypes.push_back(DBDataType::INTEGER);
        data.emplace_back(std::make_shared<DBInteger>(static_cast<int64_t>(updateData->pid)));

        resultStatus = execute(sqlCommand, dataTypes, data, {}, returnedData, false);
    } else if (command->type == DBCommandType::DELETE_EMAIL) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM emails WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_MII) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM miis WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_USER) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM users WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_USER_OWNERSHIPS) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM ownerships WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_USER_AGREEMENTS) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM user_agreements WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_USER_DEVICE_ATTRIBUTES) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM device_attributes WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_AGREEMENT) {
        auto* query = std::any_cast<DBAgreementQuery>(&command->data);

        // If version is specified, delete only that version; otherwise delete all versions
        if (query->version.has_value()) {
            resultStatus = execute("DELETE FROM agreements WHERE type = ? AND version = ? AND country = ? AND language = ?;",
                                   {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING},
                                   {std::make_shared<DBString>(query->type),
                                    std::make_shared<DBInteger>(query->version.value()),
                                    std::make_shared<DBString>(query->country),
                                    std::make_shared<DBString>(query->language)},
                                   {}, returnedData, true);
        } else {
            resultStatus = execute("DELETE FROM agreements WHERE type = ? AND country = ? AND language = ?;",
                                   {DBDataType::STRING, DBDataType::STRING, DBDataType::STRING},
                                   {std::make_shared<DBString>(query->type),
                                    std::make_shared<DBString>(query->country),
                                    std::make_shared<DBString>(query->language)},
                                   {}, returnedData, true);
        }
    } else if (command->type == DBCommandType::DELETE_FRIEND) {
        auto* query = std::any_cast<DBFriendDeleteQuery>(&command->data);

        resultStatus = execute("DELETE FROM friendships WHERE pid = ? AND friend_pid = ? OR "
                               "pid = ? AND friend_pid = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->friendPid),
                                std::make_shared<DBInteger>(query->friendPid), std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_FRIEND_REQUEST) {
        auto* query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM friend_requests WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_PERSISTENT_NOTIFICATION) {
        auto* query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM notifications WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::UNBLOCK_FRIEND) {
        auto* query = std::any_cast<DBFriendDeleteQuery>(&command->data);

        resultStatus = execute("DELETE FROM blocks WHERE pid = ? AND blocked_pid = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->friendPid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::COUNT_AGREEMENTS) {
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

        resultStatus = execute(sqlCommand, dataTypes, data, {DBDataType::INTEGER}, returnedData, false);

        if (resultStatus == DBResultStatus::SUCCESS) {
            if (!returnedData->empty() && !(*returnedData)[0].empty()) {
                resultsData = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>((*returnedData)[0][0])->data);
            } else {
                resultsData = static_cast<int64_t>(0);
            }
        }
    } else if (command->type == DBCommandType::GET_SIGNED_AGREEMENTS) {
        auto *query = std::any_cast<DBUserAgreementsQuery>(&command->data);

        resultStatus = execute("SELECT type, version, country, signed_date FROM user_agreements WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING, DBDataType::DATETIME},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
    } else if (command->type == DBCommandType::LIST_DEVICES) {
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

        resultStatus = execute(sqlCommand, dataTypes, data,
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::STRING, DBDataType::STRING,
                                DBDataType::INTEGER, DBDataType::DATETIME},
                               returnedData, false);

        if (resultStatus == DBResultStatus::SUCCESS) {
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
        }
    } else if (command->type == DBCommandType::LIST_ACCOUNTS) {
        auto *query = std::any_cast<DBAccountsListQuery>(&command->data);

        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        std::string sqlCommand = "SELECT u.pid, u.username, u.password, e.id, e.address, e.parent, e.\"primary\", "
                                 "e.reachable, e.type, e.updated_by, e.validated, e.validated_date, e.validation_code, "
                                 "m.id, m.hash, m.name, m.\"primary\", m.data, u.gender, u.region, u.tz, u.language, "
                                 "u.active, u.marketing, u.off_device, u.birth_date, u.country, u.create_date, u.last_updated, u.is_admin "
                                 "FROM users u "
                                 "LEFT JOIN emails e ON u.email_id = e.id "
                                 "LEFT JOIN miis m ON u.mii_id = m.id";

        bool whereAdded = false;
        if (query->username.has_value()) {
            sqlCommand += " WHERE u.username " + caseInsensitiveLikeOperator() + " ?";
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

        resultStatus = execute(sqlCommand, dataTypes, data,
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER,
                                DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::INTEGER, DBDataType::DATETIME,
                                DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING,
                                DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING,
                                DBDataType::STRING, DBDataType::INTEGER, DBDataType::INTEGER,
                                DBDataType::INTEGER, DBDataType::STRING, DBDataType::STRING,
                                DBDataType::DATETIME, DBDataType::DATETIME, DBDataType::INTEGER},
                               returnedData, false);

        if (resultStatus == DBResultStatus::SUCCESS) {
            std::vector<DBUserProfileData> accounts;
            for (const auto& row : *returnedData) {
                DBUserProfileData account {
                    .pid = static_cast<uint32_t>(std::any_cast<int64_t>(row[0]->data)),
                    .username = std::any_cast<std::string>(row[1]->data),
                    .emailId = row[3]->type == DBDataType::NULL_T ? 0 : std::any_cast<int64_t>(row[3]->data),
                    .miiId = row[13]->type == DBDataType::NULL_T ? 0 : std::any_cast<int64_t>(row[13]->data),
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
                    .email = row[4]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[4]->data),
                    .emailParent = row[5]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[5]->data)),
                    .emailPrimary = row[6]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[6]->data)),
                    .emailReachable = row[7]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[7]->data)),
                    .emailType = row[8]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[8]->data),
                    .emailUpdatedBy = row[9]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[9]->data),
                    .emailValidated = row[10]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[10]->data)),
                    .emailValidatedDate = row[11]->type == DBDataType::NULL_T ? datetime_t{} : std::any_cast<datetime_t>(row[11]->data),
                    .emailValidationCode = row[12]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[12]->data),
                    .miiName = row[15]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[15]->data),
                    .miiData = row[17]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[17]->data),
                    .miiPrimary = row[16]->type == DBDataType::NULL_T ? false : static_cast<bool>(std::any_cast<int64_t>(row[16]->data)),
                    .miiHash = row[14]->type == DBDataType::NULL_T ? "" : std::any_cast<std::string>(row[14]->data),
                    .isAdmin = static_cast<bool>(std::any_cast<int64_t>(row[29]->data))
                };
                accounts.push_back(std::move(account));
            }

            resultsData = std::move(accounts);
        }
    } else if (command->type == DBCommandType::DELETE_USER_DEVICE_ATTRIBUTE) {
        auto *query = std::any_cast<DBDeviceAttributeDeleteQuery>(&command->data);

        resultStatus = execute("DELETE FROM device_attributes WHERE device_id = ? AND pid = ? AND name = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING},
                               {std::make_shared<DBInteger>(query->deviceId),
                                std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBString>(query->name)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_DEVICE_ATTRIBUTES_FOR_OWNERSHIP) {
        auto *query = std::any_cast<DBOwnershipDeleteQuery>(&command->data);

        resultStatus = execute("DELETE FROM device_attributes WHERE device_id = ? AND pid = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->deviceId),
                                std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_DEVICE) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM devices WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_DEVICE_OWNERSHIPS) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM ownerships WHERE device_id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_DEVICE_ATTRIBUTES) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM device_attributes WHERE device_id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_OWNERSHIP) {
        auto *query = std::any_cast<DBOwnershipDeleteQuery>(&command->data);

        resultStatus = execute("DELETE FROM ownerships WHERE pid = ? AND device_id = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBInteger>(query->deviceId)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_USER_AGREEMENT) {
        auto *query = std::any_cast<DBUserAgreementDeleteQuery>(&command->data);

        resultStatus = execute("DELETE FROM user_agreements WHERE pid = ? AND type = ? AND version = ? AND country = ?;",
                               {DBDataType::INTEGER, DBDataType::STRING, DBDataType::INTEGER, DBDataType::STRING},
                               {std::make_shared<DBInteger>(query->pid),
                                std::make_shared<DBString>(query->type),
                                std::make_shared<DBInteger>(query->version),
                                std::make_shared<DBString>(query->country)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_GAME_SERVER_ACCESS) {
        auto *query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM game_server_access WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INACTIVATE_ALL_USER_OWNERSHIPS) {
        auto *query = std::any_cast<DBPidQuery>(&command->data);

        datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

        resultStatus = execute("UPDATE ownerships SET status = 'INACTIVE', last_updated = ? WHERE pid = ?;",
                               {DBDataType::DATETIME, DBDataType::INTEGER},
                               {std::make_shared<DBDateTime>(now), std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::COUNT_DEVICES) {
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

        resultStatus = execute(sqlCommand, dataTypes, data, {DBDataType::INTEGER}, returnedData, false);

        if (resultStatus == DBResultStatus::SUCCESS) {
            if (!returnedData->empty() && !(*returnedData)[0].empty()) {
                resultsData = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>((*returnedData)[0][0])->data);
            } else {
                resultsData = static_cast<int64_t>(0);
            }
        }
    } else if (command->type == DBCommandType::COUNT_ACCOUNTS) {
        auto *query = std::any_cast<DBAccountsListQuery>(&command->data);

        std::vector<DBDataType> dataTypes;
        std::vector<std::shared_ptr<DBData>> data;

        std::string sqlCommand = "SELECT COUNT(*) FROM users u";

        bool whereAdded = false;
        if (query->username.has_value()) {
            sqlCommand += " WHERE u.username " + caseInsensitiveLikeOperator() + " ?";
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

        resultStatus = execute(sqlCommand, dataTypes, data, {DBDataType::INTEGER}, returnedData, false);

        if (resultStatus == DBResultStatus::SUCCESS) {
            if (!returnedData->empty() && !(*returnedData)[0].empty()) {
                resultsData = std::any_cast<int64_t>(std::dynamic_pointer_cast<DBInteger>((*returnedData)[0][0])->data);
            } else {
                resultsData = static_cast<int64_t>(0);
            }
        }
    } else if (command->type == DBCommandType::DELETE_ALL_BLOCKS_BY_PID) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM blocks WHERE pid = ? OR blocked_pid = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_ALL_FRIEND_REQUESTS_BY_PID) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM friend_requests WHERE from_pid = ? OR to_pid = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_ALL_FRIENDSHIPS_BY_PID) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM friendships WHERE pid = ? OR friend_pid = ?;",
                               {DBDataType::INTEGER, DBDataType::INTEGER},
                               {std::make_shared<DBInteger>(query->pid), std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_ALL_NOTIFICATIONS_BY_PID) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM notifications WHERE \"for\" = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::DELETE_USER_INFO_BY_PID) {
        auto* query = std::any_cast<DBPidQuery>(&command->data);

        resultStatus = execute("DELETE FROM user_info WHERE pid = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->pid)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::INSERT_TASK) {
        auto *query = std::any_cast<DBTaskInsertQuery>(&command->data);

        int64_t insertedId = -1;
        resultStatus = executeInsertReturningId("INSERT INTO pending_tasks (type, params) VALUES (?, ?);",
                                                {DBDataType::INTEGER, DBDataType::STRING},
                                                {std::make_shared<DBInteger>(query->type),
                                                 std::make_shared<DBString>(query->params)},
                                                insertedId);

        if (resultStatus == DBResultStatus::SUCCESS) {
            resultsData = insertedId;
        }
    } else if (command->type == DBCommandType::GET_ALL_TASKS) {
        resultStatus = execute("SELECT id, type, params FROM pending_tasks;", {}, {},
                               {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::STRING},
                               returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
            std::vector<DBTaskData> tasksData;
            for (const auto& row : *returnedData) {
                DBTaskData taskData {};
                taskData.id = std::any_cast<int64_t>(row[0]->data);
                taskData.type = static_cast<int>(std::any_cast<int64_t>(row[1]->data));
                taskData.params = std::any_cast<std::string>(row[2]->data);

                tasksData.push_back(taskData);
            }

            resultsData = tasksData;
        }
    } else if (command->type == DBCommandType::DELETE_TASK) {
        auto *query = std::any_cast<DBIdQuery>(&command->data);

        resultStatus = execute("DELETE FROM pending_tasks WHERE id = ?;",
                               {DBDataType::INTEGER}, {std::make_shared<DBInteger>(query->id)},
                               {}, returnedData, true);
    } else if (command->type == DBCommandType::UPLOAD_FESTIVAL_SCORE) {
        auto* query = std::any_cast<DBFestivalScoreUploadQuery>(&command->data);

        resultStatus = [&]() -> DBResultStatus {
            bool ownTransaction = !inTransaction();
            DBResultStatus status;

            auto rollback = [&] {
                if (ownTransaction) execute("ROLLBACK;", {}, {}, {}, nullptr, false);
            };

            if (ownTransaction) {
                status = execute(beginTransactionSQL(true), {}, {}, {}, nullptr, false);
                if (status != DBResultStatus::SUCCESS) return status;
            }

            // Query old team before replacing
            auto oldTeamData = std::make_unique<std::vector<std::vector<std::shared_ptr<DBData>>>>();
            status = execute("SELECT team FROM festival_user_teams WHERE festival_id = ? AND pid = ?;",
                             {DBDataType::INTEGER, DBDataType::INTEGER},
                             {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                              std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))},
                             {DBDataType::INTEGER}, oldTeamData, true);
            if (status != DBResultStatus::SUCCESS) {
                rollback();
                return status;
            }

            std::optional<uint8_t> oldTeam;
            if (!oldTeamData->empty() && !oldTeamData->at(0).empty()) {
                oldTeam = static_cast<uint8_t>(std::any_cast<int64_t>(oldTeamData->at(0).at(0)->data));
            }

            status = execute("INSERT INTO festival_user_teams (festival_id, pid, team) VALUES (?, ?, ?) "
                             "ON CONFLICT(festival_id, pid) DO UPDATE SET team = excluded.team;",
                             {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER},
                             {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                              std::make_shared<DBInteger>(static_cast<int64_t>(query->pid)),
                              std::make_shared<DBInteger>(static_cast<int64_t>(query->team))},
                             {}, nullptr, true);
            if (status != DBResultStatus::SUCCESS) {
                rollback();
                return status;
            }

            uint8_t newNormalizedTeam = (query->team == 0) ? 0 : 1;
            uint8_t oldNormalizedTeam = oldTeam.has_value() ? ((oldTeam.value() == 0) ? 0 : 1) : newNormalizedTeam;

            if (!oldTeam.has_value()) {
                // New user for this festival: increment new team's user count
                status = execute("INSERT INTO festival_team_user_counts (festival_id, team, user_count) VALUES (?, ?, 1) "
                                 "ON CONFLICT(festival_id, team) DO UPDATE SET user_count = festival_team_user_counts.user_count + 1;",
                                 {DBDataType::INTEGER, DBDataType::INTEGER},
                                 {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                                  std::make_shared<DBInteger>(static_cast<int64_t>(newNormalizedTeam))},
                                 {}, nullptr, true);
                if (status != DBResultStatus::SUCCESS) {
                    rollback();
                    return status;
                }
            } else if (oldNormalizedTeam != newNormalizedTeam) {
                // User switched teams: decrement old, increment new
                status = execute("UPDATE festival_team_user_counts SET user_count = " + greatestFunction() + "(0, user_count - 1) "
                                 "WHERE festival_id = ? AND team = ?;",
                                 {DBDataType::INTEGER, DBDataType::INTEGER},
                                 {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                                  std::make_shared<DBInteger>(static_cast<int64_t>(oldNormalizedTeam))},
                                 {}, nullptr, true);
                if (status != DBResultStatus::SUCCESS) {
                    rollback();
                    return status;
                }

                status = execute("INSERT INTO festival_team_user_counts (festival_id, team, user_count) VALUES (?, ?, 1) "
                                 "ON CONFLICT(festival_id, team) DO UPDATE SET user_count = festival_team_user_counts.user_count + 1;",
                                 {DBDataType::INTEGER, DBDataType::INTEGER},
                                 {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                                  std::make_shared<DBInteger>(static_cast<int64_t>(newNormalizedTeam))},
                                 {}, nullptr, true);
                if (status != DBResultStatus::SUCCESS) {
                    rollback();
                    return status;
                }
            }

            if (query->teamScore != 0) {
                status = execute("INSERT INTO festival_user_wins (festival_id, pid, won_matches) VALUES (?, ?, 1) "
                                 "ON CONFLICT(festival_id, pid) DO UPDATE SET won_matches = festival_user_wins.won_matches + 1;",
                                 {DBDataType::INTEGER, DBDataType::INTEGER},
                                 {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                                  std::make_shared<DBInteger>(static_cast<int64_t>(query->pid))},
                                 {}, nullptr, true);
                if (status != DBResultStatus::SUCCESS) {
                    rollback();
                    return status;
                }

                status = execute("INSERT INTO festival_team_totals (festival_id, team, total_wins) VALUES (?, ?, 1) "
                                 "ON CONFLICT(festival_id, team) DO UPDATE SET total_wins = festival_team_totals.total_wins + 1;",
                                 {DBDataType::INTEGER, DBDataType::INTEGER},
                                 {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                                  std::make_shared<DBInteger>(static_cast<int64_t>(newNormalizedTeam))},
                                 {}, nullptr, true);
                if (status != DBResultStatus::SUCCESS) {
                    rollback();
                    return status;
                }
            }

            if (ownTransaction) {
                status = execute("COMMIT;", {}, {}, {}, nullptr, false);
                if (status != DBResultStatus::SUCCESS) {
                    rollback();
                    return status;
                }
            }

            return DBResultStatus::SUCCESS;
        }();
    } else if (command->type == DBCommandType::GET_FESTIVAL_TOTALS) {
        auto* query = std::any_cast<DBFestivalIdQuery>(&command->data);

        resultStatus = execute(
                "SELECT t.team, COALESCE(c.user_count, 0) AS user_count, t.total_wins "
                "FROM festival_team_totals t "
                "LEFT JOIN festival_team_user_counts c ON t.festival_id = c.festival_id AND t.team = c.team "
                "WHERE t.festival_id = ? "
                "UNION ALL "
                "SELECT c.team, c.user_count, 0 AS total_wins "
                "FROM festival_team_user_counts c "
                "WHERE c.festival_id = ? AND c.team NOT IN (SELECT team FROM festival_team_totals WHERE festival_id = ?) "
                "ORDER BY team;",
                {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER},
                {std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                 std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId)),
                 std::make_shared<DBInteger>(static_cast<int64_t>(query->festivalId))},
                {DBDataType::INTEGER, DBDataType::INTEGER, DBDataType::INTEGER},
                returnedData, true);

        if (resultStatus == DBResultStatus::SUCCESS) {
            std::vector<DBFestivalTeamTotalsData> totalsData;
            for (const auto& row : *returnedData) {
                DBFestivalTeamTotalsData data {
                    .team = static_cast<uint8_t>(std::any_cast<int64_t>(row[0]->data)),
                    .userCount = static_cast<uint32_t>(std::any_cast<int64_t>(row[1]->data)),
                    .totalWins = static_cast<uint32_t>(std::any_cast<int64_t>(row[2]->data))
                };
                totalsData.push_back(data);
            }

            resultsData = std::move(totalsData);
        }
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::DB,
                    "Unknown command type: " + std::to_string(static_cast<int>(command->type)));
        return;
    }

    auto result = Result(resultStatus, std::move(resultsData));
    command->task->complete(std::move(result));
}

} // namespace db
