#include "friendsSecure.hpp"

#include "../../crypto/tools.hpp"
#include "../../constants.hpp"
#include "../types/common/result.hpp"
#include "../types/friendsSecure/principalPreference.hpp"
#include "../types/friendsSecure/comment.hpp"
#include "../types/friendsSecure/friendInfo.hpp"
#include "../types/friendsSecure/friendRequest.hpp"
#include "../types/friendsSecure/blacklistedPrincipal.hpp"
#include "../types/friendsSecure/persistentNotification.hpp"
#include "../auth/authUtils.hpp"
#include "../types/friendsSecure/nintendoNotificationEventGeneral.hpp"

#include <utility>

// TODO Add mutex

namespace nex::rmc {

FriendsSecureRMC::FriendsSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                                   std::string base64JWTKey) :
        Server(std::move(logger)), db(std::move(db)), base64JWTKey(std::move(base64JWTKey)) {
    logGroup = Logger::group::FRIENDS_SECURE;

    // Protocol 11 - Secure connection
    registerCall(this, &FriendsSecureRMC::registerEx, 11, 4);

    // Protocol 102 - Friends (Wii U)
    registerCall(this, &FriendsSecureRMC::updateAndGetAllInformation, 102, 1);
    registerCall(this, &FriendsSecureRMC::updatePresence, 102, 13);
}

void FriendsSecureRMC::registerEx(ClientInfo client, Request req, List<StationURL> urls, AnyDataHolder data) {
    if (data.getType() != "NintendoLoginData") {
        logger->log(Logger::level::WARN, logGroup, "Invalid data type for registerEx: " + data.getType()
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

    Result retval;
    UInt32 rvConnId;
    StationURL clientPublicUrl;

    std::vector<T_ptr> params(3);

    if (urls.size() != 1) {
        retval.success = false;
        retval.code = Error::CORE__INVALID_ARGUMENT;

        params[0] = std::make_shared<Result>(retval);
        params[1] = std::make_shared<UInt32>();
        params[1] = std::make_shared<StationURL>();

        sendMsg(client, res, params);
        return;
    }

    String jwtToken;
    try {
        jwtToken = data.get<String>();
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Malformed String in registerEx: " + std::string(e.what())
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        return;
    }

    if (!utils::checkJWT(jwtToken, base64JWTKey, FRIENDS_SERVER_ID, client, logger, logGroup)) {
        retval.code = Error::CORE__ACCESS_DENIED;
        retval.success = false;

        params[0] = std::make_shared<Result>(retval);
        params[1] = std::make_shared<UInt32>(0, rvConnId);
        params[2] = std::make_shared<StationURL>(clientPublicUrl);

        sendMsg(client, res, params);
        return;
    }

    auto getFriendsInfoCmd = db::Database::craftGetFriendsInfoCommand(client.pid);

    uint32_t cmdId = db::Database::runCommand(db, std::move(getFriendsInfoCmd), registerCloseCall, unregisterCloseCall, shouldStop);
    auto friendsInfo = db->getResult(cmdId);

    if (friendsInfo->status != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get friends info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        return;
    }

    FriendsRegisteredClientInfo clientInfo{client, {}};

    for (auto& friendDataVec : friendsInfo->data) {
        auto friendData = std::move(std::any_cast<std::vector<std::any>>(friendDataVec));

        clientInfo.friends.push_back(std::any_cast<uint32_t>(friendData[0]));
    }

    registeredClients.insert(std::make_pair(client.pid, std::move(clientInfo)));

    logger->log(Logger::level::INFO, logGroup, "Registered " + std::to_string(client.pid) + " from "
                            + util::ipv4ToString(client.address.address) + ":" + std::to_string(client.address.address.port));

    retval.code = Error::CORE__UNKNOWN;
    retval.success = true;
    rvConnId = nextRVConnId++;

    clientPublicUrl.proto = Protocol::PRUDP;
    clientPublicUrl.ip = client.address.address;
    clientPublicUrl.port = client.address.address.port;
    clientPublicUrl.natf = 0;
    clientPublicUrl.natm = 0;
    clientPublicUrl.pmp = 0;
    clientPublicUrl.sid = 15;
    clientPublicUrl.type = 3;
    clientPublicUrl.upnp = 0;

    params[0] = std::make_shared<Result>(retval);
    params[1] = std::make_shared<UInt32>(0, rvConnId);
    params[2] = std::make_shared<StationURL>(clientPublicUrl);

    sendMsg(client, res, params);
}

void FriendsSecureRMC::updateAndGetAllInformation(ClientInfo client, Request req, NNAInfo nnaInfo,
                                                  NintendoPresenceV2 presence, Datetime birthdate) {
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        return;
    }

    auto getUserInfoCmd = db::Database::craftGetUserInfoCommand(client.pid);
    auto getFriendsInfoCmd = db::Database::craftGetFriendsInfoCommand(client.pid);

    uint32_t cmdId = db::Database::runCommand(db, std::move(getUserInfoCmd), registerCloseCall, unregisterCloseCall, shouldStop);
    auto userInfo = db->getResult(cmdId);

    cmdId = db::Database::runCommand(db, std::move(getFriendsInfoCmd), registerCloseCall, unregisterCloseCall, shouldStop);
    auto friendsInfo = db->getResult(cmdId);

    if (userInfo->status != db::DBResultStatus::SUCCESS || friendsInfo->status != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        return;
    }

    if (userInfo->data.empty()) {
        logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        return;
    }

    db::datetime_t lastOnline = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(client.pid, std::nullopt, std::nullopt,
                                                                      std::nullopt, std::move(nnaInfo.encode()),
                                                                      std::move(presence.encode()),
                                                                      std::vector<uint8_t>(), lastOnline);

    cmdId = db::Database::runCommand(db, std::move(updateUserInfoCmd), registerCloseCall, unregisterCloseCall, shouldStop);
    auto updateUserInfoResult = db->getResult(cmdId);
    if (updateUserInfoResult->status != db::DBResultStatus::SUCCESS) {
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

    PrincipalPreference principalPreference(client.minorVersion);
    principalPreference.showOnline = std::any_cast<bool>(userInfo->data[0]);
    principalPreference.showPlaying = std::any_cast<bool>(userInfo->data[1]);
    principalPreference.blockFriendRequest = std::any_cast<bool>(userInfo->data[2]);

    Comment statusMsg(client.minorVersion);
    statusMsg.decode(std::any_cast<std::vector<uint8_t>>(userInfo->data[5]));

    List<FriendInfo> friendList(client.minorVersion);
    for (auto& friendDataVec : friendsInfo->data) {
        auto friendData = std::move(std::any_cast<std::vector<std::any>>(friendDataVec));

        FriendInfo friendInfo(client.minorVersion);

        friendInfo.nnaInfo.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData[5])));
        friendInfo.presence.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData[6])));
        friendInfo.comment.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData[7])));

        auto lastOnlineDate = std::any_cast<db::datetime_t>(friendData[8]);
        friendInfo.lastOnline = Datetime(0, lastOnlineDate);

        auto becameFriendsDate = std::any_cast<db::datetime_t>(friendData[9]);
        friendInfo.becameFriends = Datetime(0, becameFriendsDate);

        friendList.push_back(friendInfo);
    }

    List<FriendRequest> sentFriendRequests(client.minorVersion);
    List<FriendRequest> receivedFriendRequests(client.minorVersion);
    List<BlacklistedPrincipal> blacklistedPrincipals(client.minorVersion);
    Bool unk1;
    List<PersistentNotification> notifications(client.minorVersion);
    Bool unk2;

    std::vector<T_ptr> params(9);
    params[0] = std::make_shared<PrincipalPreference>(principalPreference);
    params[1] = std::make_shared<Comment>(statusMsg);
    params[2] = std::make_shared<List<FriendInfo>>(friendList);
    params[3] = std::make_shared<List<FriendRequest>>(sentFriendRequests);
    params[4] = std::make_shared<List<FriendRequest>>(receivedFriendRequests);
    params[5] = std::make_shared<List<BlacklistedPrincipal>>(blacklistedPrincipals);
    params[6] = std::make_shared<Bool>(0, unk1);
    params[7] = std::make_shared<List<PersistentNotification>>(notifications);
    params[8] = std::make_shared<Bool>(0, unk2);

    sendMsg(client, res, params);

    NNAInfo dbUserData(client.minorVersion);
    dbUserData.decode(std::move(std::any_cast<std::vector<uint8_t>>(userInfo->data[3])));
    bool miiChanged = nnaInfo.info.mii.encode() != dbUserData.info.mii.encode();

    // Send presence (any possibly mii change) update to connected friends
    for (auto& friendPid : clientIt->second.friends) {
        AnyDataHolder data;
        presence.pid = client.pid;
        presence.online = true;
        data.set(presence, "NintendoPresenceV2");

        auto friendIt = registeredClients.find(friendPid);
        if (friendIt == registeredClients.end()) continue;

        sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
        if (miiChanged) {
            data.set(nnaInfo, "NNAInfo");
            sendNotification(friendIt->second.client, NintendoNotificationType::MII_CHANGED, client.pid, data);
        }
    }
}

void FriendsSecureRMC::updatePresence(ClientInfo client, Request req, NintendoPresenceV2 presence) {
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        return;
    }

    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(client.pid, std::nullopt, std::nullopt,
                                                                      std::nullopt, std::vector<uint8_t>(),
                                                                      std::move(presence.encode()),
                                                                      std::vector<uint8_t>(), std::nullopt);

    uint32_t cmdId = db::Database::runCommand(db, std::move(updateUserInfoCmd), registerCloseCall, unregisterCloseCall, shouldStop);
    auto updateUserInfoResult = db->getResult(cmdId);

    if (updateUserInfoResult->status != db::DBResultStatus::SUCCESS) {
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

    // Send presence update to connected friends
    for (auto& friendPid : clientIt->second.friends) {
        AnyDataHolder data;
        presence.pid = client.pid;
        presence.online = true;
        data.set(presence, "NintendoPresenceV2");

        auto friendIt = registeredClients.find(friendPid);
        if (friendIt == registeredClients.end()) continue;

        sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
    }
}

void FriendsSecureRMC::sendNotification(ClientInfo client, NintendoNotificationType type, uint32_t sender,
                                        const AnyDataHolder& data) {
    Request req;
    req.protocolId = 100; // Nintendo Notification Event Protocol
    req.extendedProtocolId = 0;
    req.methodId = NintendoNotificationEvent::getMethodForType(type);
    req.callId = nextCallId++;

    NintendoNotificationEvent notificationEvent(client.minorVersion);
    notificationEvent.type = type;
    notificationEvent.sender = sender;
    notificationEvent.eventData = data;

    std::vector<T_ptr> params;
    params.push_back(std::make_shared<NintendoNotificationEvent>(notificationEvent));

    sendMsg(client, req, params);
}

void FriendsSecureRMC::onDisconnect(prudp::PRUDPAddress address) {
    NintendoPresenceV2 presence(0);

    auto clientIt = registeredClients.find(pidMap[address]);
    if (clientIt != registeredClients.end()) {
        db::datetime_t lastOnline = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
        auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(clientIt->second.client.pid, std::nullopt,
                                                                          std::nullopt, std::nullopt,
                                                                          std::vector<uint8_t>(),
                                                                          std::move(presence.encode()),
                                                                          std::vector<uint8_t>(), lastOnline);

        uint32_t cmdId = db::Database::runCommand(db, std::move(updateUserInfoCmd), registerCloseCall, unregisterCloseCall, shouldStop);
        auto updateUserInfoResult = db->getResult(cmdId);

        if (updateUserInfoResult->status != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(clientIt->second.client.pid)
                                                       + " from " + util::ipv4ToString(clientIt->second.client.address.address) + ":"
                                                       + std::to_string(clientIt->second.client.address.address.port));
        }

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

    Server::onDisconnect(address);
}

} // namespace nex::rmc