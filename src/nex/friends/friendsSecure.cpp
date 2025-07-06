#include "friendsSecure.hpp"

#include "../../crypto/tools.hpp"
#include "../../constants.hpp"
#include "../types/common/result.hpp"
#include "../types/friendsSecure/principalPreference.hpp"
#include "../types/friendsSecure/friendRequest.hpp"
#include "../types/friendsSecure/blacklistedPrincipal.hpp"
#include "../types/friendsSecure/persistentNotification.hpp"
#include "../auth/authUtils.hpp"
#include "../types/friendsSecure/nintendoNotificationEventGeneral.hpp"

#include <utility>

namespace nex::rmc {

FriendsSecureRMC::FriendsSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                                   std::string base64JWTKey) :
        Server(std::move(logger)), db(std::move(db)), base64JWTKey(std::move(base64JWTKey)) {
    logGroup = Logger::group::FRIENDS_SECURE;

    // Protocol 11 - Secure connection
    REGISTER_CALL(FriendsSecureRMC::registerEx, 11, 4);

    // Protocol 102 - Friends (Wii U)
    REGISTER_CALL(FriendsSecureRMC::updateAndGetAllInformation, 102, 1);
    REGISTER_CALL(FriendsSecureRMC::updatePresence, 102, 13);
}

void FriendsSecureRMC::registerEx(ClientInfo client,
                                  Request req,
                                  std::unique_ptr<List<StationURL>> urls,
                                  std::unique_ptr<AnyDataHolder> data) {
    if (data->getType() != "NintendoLoginData") {
        logger->log(Logger::level::WARN, logGroup, "Invalid data type for registerEx: " + data->getType()
                                + " from " + util::ipv4ToString(client.address.address) + ":"
                                + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true; // Even if the JWT is invalid, the error is in the %retval% field

    std::unique_ptr<Result> retval = std::make_unique<Result>();
    std::unique_ptr<UInt32> rvConnId = std::make_unique<UInt32>(0, 0);
    std::unique_ptr<StationURL> clientPublicUrl = std::make_unique<StationURL>();

    std::vector<T_ptr> params(3);

    String jwtToken;
    try {
        jwtToken = data->get<String>();
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Malformed String in registerEx: " + std::string(e.what())
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        return;
    }

    if (!utils::checkJWT(jwtToken, base64JWTKey, FRIENDS_SERVER_ID, client, logger, logGroup)) {
        retval->code = Error::CORE__ACCESS_DENIED;
        retval->success = false;

        params[0] = std::move(retval);
        params[1] = std::move(rvConnId);
        params[2] = std::move(clientPublicUrl);

        sendMsg(client, res, params);
        return;
    }

    auto friendsInfoCallback = [params = std::move(params),
                                                       retval = std::move(retval),
                                                       rvConnId = std::move(rvConnId),
                                                       clientPublicUrl = std::move(clientPublicUrl),
                                                       urls = std::move(urls),
                                                       data = std::move(data),
                                                       client, req, res, this](std::any&& resultsAny) mutable {

        std::unique_ptr<db::Result> friendsInfo = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

        if (friendsInfo->getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get friends info for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));

            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            return;
        }

        FriendsRegisteredClientInfo clientInfo{client, {}};

        for (auto& friendData : friendsInfo->getData<std::vector<db::DBFriendInfoData>>()) {
            clientInfo.friends.push_back(std::any_cast<uint32_t>(friendData.friendPid));
        }

        std::unique_lock registeredClientsLock(registeredClientsMutex);
        registeredClients.insert(std::make_pair(client.pid, std::move(clientInfo)));
        registeredClientsLock.unlock();

        logger->log(Logger::level::INFO, logGroup, "Registered " + std::to_string(client.pid) + " from "
                                                   + util::ipv4ToString(client.address.address) + ":" + std::to_string(client.address.address.port));

        retval->code = Error::CORE__UNKNOWN;
        retval->success = true;
        std::unique_lock rvConnIdLock(rvConnIdMutex);
        *rvConnId = nextRVConnId++;
        rvConnIdLock.unlock();

        clientPublicUrl->proto = Protocol::PRUDP;
        clientPublicUrl->ip = client.address.address;
        clientPublicUrl->port = client.address.address.port;
        clientPublicUrl->natf = 0;
        clientPublicUrl->natm = 0;
        clientPublicUrl->pmp = 0;
        clientPublicUrl->sid = 15;
        clientPublicUrl->type = 3;
        clientPublicUrl->upnp = 0;

        params[0] = std::move(retval);
        params[1] = std::move(rvConnId);
        params[2] = std::move(clientPublicUrl);

        sendMsg(client, res, params);
    };

    auto getUserInfoCmd = db::Database::craftGetUserInfoCommand(client.pid);
    db->runCommand(std::move(getUserInfoCmd), queueMutex, queueCV, promisesQueue)
        ->setContext(std::pair<ClientInfo, Request>(client, req))
        .then([friendsInfoCallback = std::move(friendsInfoCallback),
               client, req, this](std::any&& resultsAny) mutable {

        std::unique_ptr<db::Result> userInfo = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

        if (userInfo->getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));

            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            return;
        }

        if (!userInfo->hasData()) {
            logger->log(Logger::level::INFO, logGroup, "User " + std::to_string(client.pid) + " was not found, registering");

            NNAInfo nnaInfo(client.minorVersion);
            NintendoPresenceV2 presence(client.minorVersion);
            Comment comment(client.minorVersion);

            auto insertUserInfoCmd = db::Database::craftInsertUserInfoCommand(
                    client.pid, true, true, false,
                    nnaInfo.encode(),
                    presence.encode(),
                    comment.encode(),
                    std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));
            db->runCommand(std::move(insertUserInfoCmd), queueMutex, queueCV, promisesQueue)
                ->setContext(std::pair<ClientInfo, Request>(client, req))
                .then([friendsInfoCallback = std::move(friendsInfoCallback),
                       client, req, this](std::any&& resultsAny) mutable {

                    std::unique_ptr<db::Result> insertUserInfoResult = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

                    if (insertUserInfoResult->getStatus() != db::DBResultStatus::SUCCESS) {
                        logger->log(Logger::level::WARN, logGroup, "Failed to insert user info for " + std::to_string(client.pid)
                                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                                   + std::to_string(client.address.address.port));

                        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                        return;
                    }

                    friendsInfoCallback(std::move(std::make_any<db::Result>(db::DBResultStatus::SUCCESS,
                                                                            std::vector<db::DBFriendInfoData>{})));
                });
        } else {
            auto getFriendsInfoCmd = db::Database::craftGetFriendsInfoCommand(client.pid);
            db->runCommand(std::move(getFriendsInfoCmd), queueMutex, queueCV, promisesQueue)
                ->setContext(std::pair<ClientInfo, Request>(client, req)).then(std::move(friendsInfoCallback));
        }
    });
}

void FriendsSecureRMC::updateAndGetAllInformation(ClientInfo client, Request req,
                                                  std::unique_ptr<NNAInfo> nnaInfo,
                                                  std::unique_ptr<NintendoPresenceV2> presence,
                                                  std::unique_ptr<Datetime> birthdate) {
    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        return;
    }
    registeredClientsLock.unlock();

    auto getUserInfoCmd = db::Database::craftGetUserInfoCommand(client.pid);

    db->runCommand(std::move(getUserInfoCmd), queueMutex, queueCV, promisesQueue)
        ->setContext(std::pair<ClientInfo, Request>(client, req))
        .then([nnaInfo = std::move(nnaInfo),
                presence = std::move(presence),
                client, req, this](std::any&& resultsAny) mutable {

        std::unique_ptr<db::Result> userInfo = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

        auto getFriendsInfoCmd = db::Database::craftGetFriendsInfoCommand(client.pid);

        db->runCommand(std::move(getFriendsInfoCmd), queueMutex, queueCV, promisesQueue)
            ->setContext(std::pair<ClientInfo, Request>(client, req))
            .then([userInfo = std::move(userInfo),
                    nnaInfo = std::move(nnaInfo),
                    presence = std::move(presence),
                    client, req, this](std::any&& resultsAny) mutable {

            std::unique_ptr<db::Result> friendsInfo = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

            if (userInfo->getStatus() != db::DBResultStatus::SUCCESS || friendsInfo->getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));

                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                return;
            }

            if (!userInfo->hasData()) {
                logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");

                sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
                return;
            }

            db::datetime_t lastOnline = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(client.pid, std::nullopt, std::nullopt,
                                                                              std::nullopt, std::move(nnaInfo->encode()),
                                                                              std::move(presence->encode()),
                                                                              std::nullopt, lastOnline);

            db->runCommand(std::move(updateUserInfoCmd), queueMutex, queueCV, promisesQueue)
                ->setContext(std::pair<ClientInfo, Request>(client, req))
                .then([userInfo = std::move(userInfo),
                        friendsInfo = std::move(friendsInfo),
                        nnaInfo = std::move(nnaInfo),
                        presence = std::move(presence),
                        client, req, this](std::any&& resultsAny) mutable{

                std::unique_ptr<db::Result> updateUserInfoResult = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

                if (updateUserInfoResult->getStatus() != db::DBResultStatus::SUCCESS) {
                    logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(client.pid)
                                                               + " from " + util::ipv4ToString(client.address.address) + ":"
                                                               + std::to_string(client.address.address.port));

                    sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                    return;
                }

                Response res;
                res.protocolId = req.protocolId;
                res.extendedProtocolId = req.extendedProtocolId;
                res.methodId = req.methodId;
                res.callId = req.callId;
                res.success = true;

                auto userInfoData = userInfo->getData<db::DBUserInfoData>();

                std::unique_ptr<PrincipalPreference> principalPreference = std::make_unique<PrincipalPreference>(client.minorVersion);
                principalPreference->showOnline = userInfoData.showPresence;
                principalPreference->showPlaying = userInfoData.showPlaying;
                principalPreference->blockFriendRequest = userInfoData.blockRequests;

                std::unique_ptr<Comment> statusMsg = std::make_unique<Comment>(client.minorVersion);
                statusMsg->decode(userInfoData.comment);

                std::unique_ptr<List<FriendInfo>> friendList = std::make_unique<List<FriendInfo>>(client.minorVersion);
                for (auto& friendData : friendsInfo->getData<std::vector<db::DBFriendInfoData>>()) {
                    FriendInfo friendInfo(client.minorVersion);

                    friendInfo.nnaInfo.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData.nnaInfo)));
                    friendInfo.presence.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData.presence)));
                    friendInfo.comment.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData.comment)));
                    friendInfo.lastOnline = Datetime(0, friendData.lastOnline);
                    friendInfo.becameFriends = Datetime(0, friendData.becameFriends);

                    friendList->push_back(std::move(friendInfo));
                }

                std::unique_ptr<List<FriendRequest>> sentFriendRequests = std::make_unique<List<FriendRequest>>(client.minorVersion);
                std::unique_ptr<List<FriendRequest>> receivedFriendRequests = std::make_unique<List<FriendRequest>>(client.minorVersion);
                std::unique_ptr<List<BlacklistedPrincipal>> blacklistedPrincipals = std::make_unique<List<BlacklistedPrincipal>>(client.minorVersion);
                std::unique_ptr<Bool> unk1 = std::make_unique<Bool>(client.minorVersion, false);
                std::unique_ptr<List<PersistentNotification>> notifications = std::make_unique<List<PersistentNotification>>(client.minorVersion);
                std::unique_ptr<Bool> unk2 = std::make_unique<Bool>(client.minorVersion, false);

                std::vector<T_ptr> params(9);
                params[0] = std::move(principalPreference);
                params[1] = std::move(statusMsg);
                params[2] = std::move(friendList);
                params[3] = std::move(sentFriendRequests);
                params[4] = std::move(receivedFriendRequests);
                params[5] = std::move(blacklistedPrincipals);
                params[6] = std::move(unk1);
                params[7] = std::move(notifications);
                params[8] = std::move(unk2);

                sendMsg(client, res, params);

                NNAInfo dbUserData(client.minorVersion);
                dbUserData.decode(userInfoData.nnaInfo);
                bool miiChanged = nnaInfo->info.mii.encode() != dbUserData.info.mii.encode();

                // Send presence (any possibly mii change) update to connected friends
                std::unique_lock registeredClientsLock(registeredClientsMutex);
                auto clientIt = registeredClients.find(client.pid);
                for (auto& friendPid : clientIt->second.friends) {
                    AnyDataHolder data;
                    presence->pid = client.pid;
                    presence->online = true;
                    data.set(*presence, "NintendoPresenceV2");

                    auto friendIt = registeredClients.find(friendPid);
                    if (friendIt == registeredClients.end()) continue;

                    sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
                    if (miiChanged) {
                        data.set(*nnaInfo, "NNAInfo");
                        sendNotification(friendIt->second.client, NintendoNotificationType::MII_CHANGED, client.pid, data);
                    }
                }
            });
        });
    });
}

void FriendsSecureRMC::updatePresence(ClientInfo client, Request req, std::unique_ptr<NintendoPresenceV2> presence) {
    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        return;
    }
    registeredClientsLock.unlock();

    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(client.pid, std::nullopt, std::nullopt,
                                                                      std::nullopt, std::nullopt,
                                                                      std::move(presence->encode()),
                                                                      std::nullopt, std::nullopt);

    db->runCommand(std::move(updateUserInfoCmd), queueMutex, queueCV, promisesQueue)
        ->setContext(std::pair<ClientInfo, Request>(client, req))
        .then([presence = std::move(presence),
               client, req, this](std::any&& resultsAny) {

        std::unique_ptr<db::Result> updateUserInfoResult = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

        if (updateUserInfoResult->getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));

            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            return;
        }

        Response res;
        res.protocolId = req.protocolId;
        res.extendedProtocolId = req.extendedProtocolId;
        res.methodId = req.methodId;
        res.callId = req.callId;
        res.success = true;

        sendMsg(client, res, {});

        std::unique_lock registeredClientsLock(registeredClientsMutex);
        auto clientIt = registeredClients.find(client.pid);
        // Send presence update to connected friends
        for (auto& friendPid : clientIt->second.friends) {
            AnyDataHolder data;
            presence->pid = client.pid;
            presence->online = true;
            data.set(*presence, "NintendoPresenceV2");

            auto friendIt = registeredClients.find(friendPid);
            if (friendIt == registeredClients.end()) continue;

            sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
        }
    });
}

void FriendsSecureRMC::sendNotification(ClientInfo client, NintendoNotificationType type, uint32_t sender,
                                        const AnyDataHolder& data) {
    Request req;
    req.protocolId = 100; // Nintendo Notification Event Protocol
    req.extendedProtocolId = 0;
    req.methodId = NintendoNotificationEvent::getMethodForType(type);
    std::unique_lock callIdLock(callIdMutex);
    req.callId = nextCallId++;
    callIdLock.unlock();

    NintendoNotificationEvent notificationEvent(client.minorVersion);
    notificationEvent.type = type;
    notificationEvent.sender = sender;
    notificationEvent.eventData = data;

    std::vector<T_ptr> params;
    params.push_back(std::make_unique<NintendoNotificationEvent>(notificationEvent));

    sendMsg(client, req, params);
}

void FriendsSecureRMC::onDisconnect(prudp::PRUDPAddress address) {
    NintendoPresenceV2 presence(0);

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(pidMap[address]);
    if (clientIt != registeredClients.end()) {
        db::datetime_t lastOnline = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
        auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(clientIt->second.client.pid, std::nullopt,
                                                                          std::nullopt, std::nullopt,
                                                                          std::nullopt,
                                                                          std::move(presence.encode()),
                                                                          std::nullopt, lastOnline);

        db->runCommand(std::move(updateUserInfoCmd), queueMutex, queueCV, promisesQueue)
            ->then([address, this](std::any&& resultsAny) {

            std::unique_ptr<db::Result> updateUserInfoResult = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

            std::unique_lock registeredClientsLock(registeredClientsMutex);
            auto clientIt = registeredClients.find(pidMap[address]);

            if (updateUserInfoResult->getStatus() != db::DBResultStatus::SUCCESS && clientIt != registeredClients.end()) {
                logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(clientIt->second.client.pid)
                                                           + " from " + util::ipv4ToString(clientIt->second.client.address.address) + ":"
                                                           + std::to_string(clientIt->second.client.address.address.port));
            }
        });

        // Send presence update to connected friends
        NintendoNotificationEventGeneral generalEvent(0);
        generalEvent.u64_param2.decode(Datetime(0, lastOnline).encode());

        for (auto& friendPid : clientIt->second.friends) {
            AnyDataHolder data;
            data.set(generalEvent, "NintendoNotificationEventGeneral");

            auto friendIt = registeredClients.find(friendPid);
            if (friendIt == registeredClients.end()) continue;

            sendNotification(friendIt->second.client, NintendoNotificationType::WENT_OFFLINE, clientIt->second.client.pid, data);
        }

        registeredClients.erase(clientIt);
    }
    registeredClientsLock.unlock();

    Server::onDisconnect(address);
}

} // namespace nex::rmc