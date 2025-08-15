#include "friendsSecure.hpp"

#include <utility>

#include "../../crypto/tools.hpp"
#include "../../constants.hpp"
#include "../types/common/result.hpp"
#include "../types/friendsSecure/principalPreference.hpp"
#include "../types/friendsSecure/friendRequest.hpp"
#include "../types/friendsSecure/blacklistedPrincipal.hpp"
#include "../types/friendsSecure/persistentNotification.hpp"
#include "../auth/authUtils.hpp"
#include "../types/friendsSecure/nintendoNotificationEventGeneral.hpp"
#include "../types/friendsSecure/comment.hpp"
#include "../types/friendsSecure/friendInfo.hpp"
#include "../types/friendsSecure/nintendoCreateAccountData.hpp"
#include "../types/friendsSecure/principalRequestBlockSetting.hpp"

namespace nex::rmc {

using namespace async;

FriendsSecureRMC::FriendsSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                                   std::string base64JWTKey) :
        Server(std::move(logger)), db(std::move(db)), base64JWTKey(std::move(base64JWTKey)) {
    logGroup = Logger::group::FRIENDS_SECURE;

    // Protocol 11 - Secure connection
    REGISTER_CALL(FriendsSecureRMC::register_, 11, 1);
    REGISTER_CALL(FriendsSecureRMC::registerEx, 11, 4);

    // Protocol 25 - Account Management
    REGISTER_CALL(FriendsSecureRMC::nintendoCreateAccount, 25, 27);

    // Protocol 102 - Friends (Wii U)
    REGISTER_CALL(FriendsSecureRMC::updateAndGetAllInformation, 102, 1);
    REGISTER_CALL(FriendsSecureRMC::addFriend, 102, 2);
    REGISTER_CALL(FriendsSecureRMC::removeFriend, 102, 4);
    REGISTER_CALL(FriendsSecureRMC::updatePresence, 102, 13);
    REGISTER_CALL(FriendsSecureRMC::updateMii, 102, 14);
    REGISTER_CALL(FriendsSecureRMC::updateComment, 102, 15);
    REGISTER_CALL(FriendsSecureRMC::updatePreference, 102, 16);
    REGISTER_CALL(FriendsSecureRMC::getBasicInfo, 102, 17);
    REGISTER_CALL(FriendsSecureRMC::checkSettingStatus, 102, 19);
    REGISTER_CALL(FriendsSecureRMC::getRequestBlockSettings, 102, 20);
}

Task<void> FriendsSecureRMC::register_(ClientInfo client,
                                       Request req,
                                       std::unique_ptr<List<StationURL>> urls) {
    if (client.pid != 100) { // Regular users should use registerEx
        logger->log(Logger::level::INFO, logGroup, "Non-guest user tried to register from "
                                                   + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    std::unique_ptr<Result> retval = std::make_unique<Result>();
    std::unique_ptr<UInt32> rvConnId = std::make_unique<UInt32>(0, 0);
    std::unique_ptr<StationURL> clientPublicUrl = std::make_unique<StationURL>();

    std::vector<T_ptr> params(3);

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

    logger->log(Logger::level::INFO, logGroup, "Registered guest user from "
                                               + util::ipv4ToString(client.address.address) + ":"
                                               + std::to_string(client.address.address.port));
    co_return;
}

Task<void> FriendsSecureRMC::registerEx(ClientInfo client,
                                        Request req,
                                        std::unique_ptr<List<StationURL>> urls,
                                        std::unique_ptr<AnyDataHolder> data) {
    if (client.pid == 100) { // Guest users are not allowed to register
        logger->log(Logger::level::INFO, logGroup, "Guest user tried to registerEx from "
                                                   + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    if (data->getType() != "NintendoLoginData") {
        logger->log(Logger::level::WARN, logGroup, "Invalid data type for registerEx: " + data->getType()
                                + " from " + util::ipv4ToString(client.address.address) + ":"
                                + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        co_return;
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
        jwtToken = std::move(data->get<String>());
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Malformed String in registerEx: " + std::string(e.what())
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        co_return;
    }

    std::string username;
    if (!utils::checkJWT(jwtToken, base64JWTKey, FRIENDS_SERVER_ID, client, logger, logGroup, username)) {
        retval->code = Error::CORE__ACCESS_DENIED;
        retval->success = false;

        params[0] = std::move(retval);
        params[1] = std::move(rvConnId);
        params[2] = std::move(clientPublicUrl);

        sendMsg(client, res, params);
        co_return;
    }

    auto getUserInfoCmd = db::Database::craftGetUserInfoByPidCommand(client.pid);
    const db::Result getUserInfoResult = co_await db->runCommand(std::move(getUserInfoCmd));

    if (getUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    std::vector<db::DBFriendInfoData> friendsInfo;

    UserPreference pref;
    if (!getUserInfoResult.hasData()) {
        logger->log(Logger::level::INFO, logGroup, "User " + std::to_string(client.pid) + " was not found, registering");

        NNAInfo nnaInfo(client.minorVersion);
        NintendoPresenceV2 presence(client.minorVersion);
        Comment comment(client.minorVersion);

        pref = UserPreference{
            true, // showOnline
            true, // showPlaying
            false // blockFriendRequest
        };

        auto insertUserInfoCmd = db::Database::craftInsertUserInfoCommand(
                client.pid,
                username,
                true, true, false,
                nnaInfo.encode(),
                presence.encode(),
                comment.encode(),
                std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));

        db::Result insertUserInfoResult = co_await db->runCommand(std::move(insertUserInfoCmd));
        if (insertUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to insert user info for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));

            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }
    } else {
        auto userData = getUserInfoResult.getData<db::DBUserInfoData>();
        pref = UserPreference{
            userData.showPresence,
            userData.showPlaying,
            userData.blockRequests
        };

        auto getFriendsInfoCmd = db::Database::craftGetFriendsInfoCommand(client.pid);
        const db::Result friendsInfoResult = co_await db->runCommand(std::move(getFriendsInfoCmd));
        if (friendsInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get friends info for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));

            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        friendsInfo = friendsInfoResult.getData<std::vector<db::DBFriendInfoData>>();
    }

    FriendsRegisteredClientInfo clientInfo{client, {client.pid, pref}, {}};

    for (auto& friendData : friendsInfo) {
        clientInfo.friends.push_back({friendData.friendPid, {friendData.showPresence, friendData.showPlaying, friendData.blockRequests}});
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
}

Task<void> FriendsSecureRMC::nintendoCreateAccount(ClientInfo client, Request req,
                                                   std::unique_ptr<String> principalName,
                                                   std::unique_ptr<String> key,
                                                   std::unique_ptr<UInt32> groups,
                                                   std::unique_ptr<String> email,
                                                   std::unique_ptr<AnyDataHolder> data) {
    if (data->getType() != "NintendoCreateAccountData") {
        logger->log(Logger::level::WARN, logGroup, "Invalid data type for nintendoCreateAccount: " + data->getType()
                                + " from " + util::ipv4ToString(client.address.address) + ":"
                                + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        co_return;
    }

    NintendoCreateAccountData createData(client.minorVersion);
    try {
        createData = std::move(data->get<NintendoCreateAccountData>());
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Malformed NintendoCreateAccountData in nintendoCreateAccount: " + std::string(e.what())
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        co_return;
    }

    client.pid = createData.nnaInfo.info.pid;

    std::string username;
    if (!utils::checkJWT(createData.nexToken, base64JWTKey, FRIENDS_SERVER_ID, client, logger, logGroup, username)) {
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    auto getUserInfoCmd = db::Database::craftGetUserInfoByPidCommand(client.pid);
    const db::Result getUserInfoResult = co_await db->runCommand(std::move(getUserInfoCmd));

    if (getUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (getUserInfoResult.hasData()) {
        logger->log(Logger::level::INFO, logGroup, "User " + std::to_string(client.pid) + " already exists, not creating account");

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__USERNAME_ALREADY_EXISTS), {});
        co_return;
    }

    logger->log(Logger::level::INFO, logGroup, "Creating account for " + std::to_string(client.pid)
                                               + " from " + util::ipv4ToString(client.address.address) + ":"
                                               + std::to_string(client.address.address.port));

    NintendoPresenceV2 presence(client.minorVersion);
    Comment comment(client.minorVersion);

    auto insertUserInfoCmd = db::Database::craftInsertUserInfoCommand(
            client.pid,
            username,
            true, true, false,
            createData.nnaInfo.encode(),
            presence.encode(),
            comment.encode(),
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));
    auto insertUserInfoResult = co_await db->runCommand(std::move(insertUserInfoCmd));
    if (insertUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to insert user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    std::vector<T_ptr> params(2);
    params[0] = std::make_unique<PID>(client.minorVersion, client.pid);
    params[1] = std::make_unique<String>(); // The PIDHMAC is useless in the Wii U, so we just send an empty String

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    sendMsg(client, res, params);
}


Task<void> FriendsSecureRMC::updateAndGetAllInformation(ClientInfo client, Request req,
                                                        std::unique_ptr<NNAInfo> nnaInfo,
                                                        std::unique_ptr<NintendoPresenceV2> presence,
                                                        std::unique_ptr<Datetime> birthdate) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }
    registeredClientsLock.unlock();

    if (!co_await cleanupExpiredFriendRequests(client.pid)) {
        logger->log(Logger::level::WARN, logGroup, "Failed to cleanup expired friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    auto getUserInfoCmd = db::Database::craftGetUserInfoByPidCommand(client.pid);
    const db::Result getUserInfoResult = co_await db->runCommand(std::move(getUserInfoCmd));

    auto getFriendsInfoCmd = db::Database::craftGetFriendsInfoCommand(client.pid);
    const db::Result friendsInfoResult = co_await db->runCommand(std::move(getFriendsInfoCmd));

    auto getSentFriendRequestsCmd = db::Database::craftGetSentFriendRequestsCommand(client.pid);
    const db::Result sentFriendRequestsResult = co_await db->runCommand(std::move(getSentFriendRequestsCmd));

    auto getReceivedFriendRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(client.pid);
    const db::Result receivedFriendRequestsResult = co_await db->runCommand(std::move(getReceivedFriendRequestsCmd));

    auto getBlockedUsersCmd = db::Database::craftGetBlockedFriendsCommand(client.pid);
    const db::Result blockedUsersResult = co_await db->runCommand(std::move(getBlockedUsersCmd));

    auto getNotificationsCmd = db::Database::craftGetPersistentNotificationsCommand(client.pid);
    const db::Result notificationsResult = co_await db->runCommand(std::move(getNotificationsCmd));

    if (getUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS
        || friendsInfoResult.getStatus() != db::DBResultStatus::SUCCESS
        || sentFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS
        || receivedFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS
        || blockedUsersResult.getStatus() != db::DBResultStatus::SUCCESS
        || notificationsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (!getUserInfoResult.hasData()) {
        logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    presence->online = true; // For some reason the Wii U sends it as false, but the server stores it as true

    db::datetime_t lastOnline = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(client.pid, std::nullopt,
                                                                      std::nullopt, std::nullopt,
                                                                      std::nullopt, std::move(nnaInfo->encode()),
                                                                      std::move(presence->encode()),
                                                                      std::nullopt, lastOnline);
    const db::Result updateUserInfoResult = co_await db->runCommand(std::move(updateUserInfoCmd));
    if (updateUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    auto userInfoData = getUserInfoResult.getData<db::DBUserInfoData>();

    std::unique_ptr<PrincipalPreference> principalPreference = std::make_unique<PrincipalPreference>(client.minorVersion);
    principalPreference->showOnline = userInfoData.showPresence;
    principalPreference->showPlaying = userInfoData.showPlaying;
    principalPreference->blockFriendRequest = userInfoData.blockRequests;

    std::unique_ptr<Comment> statusMsg = std::make_unique<Comment>(client.minorVersion);
    statusMsg->decode(userInfoData.comment);

    std::unique_ptr<List<FriendInfo>> friendList = std::make_unique<List<FriendInfo>>(client.minorVersion);
    for (auto& friendData : friendsInfoResult.getData<std::vector<db::DBFriendInfoData>>()) {
        FriendInfo friendInfo(client.minorVersion);

        friendInfo.nnaInfo.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData.nnaInfo)));
        friendInfo.presence.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData.presence)));
        friendInfo.comment.decode(std::move(std::any_cast<std::vector<uint8_t>>(friendData.comment)));
        friendInfo.lastOnline = Datetime(0, friendData.lastOnline);
        friendInfo.becameFriends = Datetime(0, friendData.becameFriends);

        friendList->push_back(std::move(friendInfo));
    }

    auto year2000 = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::from_time_t(946684800));

    std::unique_ptr<List<FriendRequest>> sentFriendRequests = std::make_unique<List<FriendRequest>>(client.minorVersion);
    for (auto& requestData : sentFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        FriendRequest request(client.minorVersion);

        NNAInfo reqNNAInfo(client.minorVersion);
        reqNNAInfo.decode(std::move(*requestData.nnaInfo));

        FriendRequestMsg reqMsg(client.minorVersion);
        reqMsg.decode(std::move(requestData.data));

        request.principalBasicInfo = reqNNAInfo.info;
        request.friendRequestMsg = reqMsg;

        if (reqMsg.expiresOn > year2000)
            request.friendRequestMsg.id = requestData.id;
        request.sentOn = Datetime(0, requestData.createdAt);

        sentFriendRequests->push_back(std::move(request));
    }

    std::unique_ptr<List<FriendRequest>> receivedFriendRequests = std::make_unique<List<FriendRequest>>(client.minorVersion);
    for (auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        FriendRequest request(client.minorVersion);

        NNAInfo reqNNAInfo(client.minorVersion);
        reqNNAInfo.decode(std::move(*requestData.nnaInfo));

        FriendRequestMsg reqMsg(client.minorVersion);
        reqMsg.decode(std::move(requestData.data));

        if (reqMsg.expiresOn <= year2000) continue;

        request.principalBasicInfo = reqNNAInfo.info;
        request.friendRequestMsg = reqMsg;
        request.friendRequestMsg.id = requestData.id;
        request.sentOn = Datetime(0, requestData.createdAt);

        receivedFriendRequests->push_back(std::move(request));
    }

    std::unique_ptr<List<BlacklistedPrincipal>> blacklistedPrincipals = std::make_unique<List<BlacklistedPrincipal>>(client.minorVersion);
    for (auto& blockedData : blockedUsersResult.getData<std::vector<db::DBBlockData>>()) {
        BlacklistedPrincipal blockedPrincipal(client.minorVersion);

        NNAInfo blockedNNAInfo(client.minorVersion);
        blockedNNAInfo.decode(std::move(blockedData.nnaInfo));

        GameKey blockedGameKey(client.minorVersion);
        blockedGameKey.decode(std::move(blockedData.gameKey));

        blockedPrincipal.principalBasicInfo = blockedNNAInfo.info;
        blockedPrincipal.gameKey = std::move(blockedGameKey);
        blockedPrincipal.blacklistedSince = Datetime(0, blockedData.createdAt);

        blacklistedPrincipals->push_back(std::move(blockedPrincipal));
    }

    std::unique_ptr<Bool> unk1 = std::make_unique<Bool>(client.minorVersion, false);
    std::unique_ptr<List<PersistentNotification>> notifications = std::make_unique<List<PersistentNotification>>(client.minorVersion);
    for (auto& notificationData : notificationsResult.getData<std::vector<db::DBPersistentNotificationData>>()) {
        PersistentNotification notification(client.minorVersion);

        notification.unk1 = notificationData.value1;
        notification.unk2 = notificationData.value2;
        notification.unk3 = notificationData.value3;
        notification.unk4 = notificationData.value4;
        notification.unk5 = notificationData.text;

        notifications->push_back(std::move(notification));
    }
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
    if (clientIt->second.userData.preference.showOnline || miiChanged) {
        registeredClientsLock.lock();
        clientIt = registeredClients.find(client.pid);
        for (auto& friendData : clientIt->second.friends) {
            AnyDataHolder data;
            presence->pid = client.pid;
            presence->online = true;
            data.set(*presence, "NintendoPresenceV2");

            auto friendIt = registeredClients.find(friendData.pid);
            if (friendIt == registeredClients.end()) continue;

            if (clientIt->second.userData.preference.showOnline && clientIt->second.userData.preference.showPlaying) {
                sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
            } else if (clientIt->second.userData.preference.showOnline) {
                NintendoPresenceV2 presenceUpdate(client.minorVersion);
                presenceUpdate.online = presence->online;
                data.set(*presence, "NintendoPresenceV2");
                sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
            }

            if (miiChanged) {
                data.set(*nnaInfo, "NNAInfo");
                sendNotification(friendIt->second.client, NintendoNotificationType::MII_CHANGED, client.pid, data);
            }
        }
    }
}

Task<void> FriendsSecureRMC::addFriend(ClientInfo client, Request req, std::unique_ptr<PID> pid) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    if (clientIt->second.friends.size() >= 100) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to add friend "
                                                       + std::to_string(*pid) + " but friend list limit exceeded");
        sendMsg(client, createError(req, Error::FPD__MY_FRIEND_LIST_LIMIT_EXCEED), {});
        co_return;
    }

    for (auto& friendData : clientIt->second.friends) {
        if (friendData.pid == *pid) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to add already existing friend "
                                                       + std::to_string(*pid));

            sendMsg(client, createError(req, Error::FPD__FRIEND_ALREADY_EXISTS), {});
            registeredClientsLock.unlock();
            co_return;
        }
    }
    registeredClientsLock.unlock();

    if (!co_await cleanupExpiredFriendRequests(client.pid)) {
        logger->log(Logger::level::WARN, logGroup, "Failed to cleanup expired friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (!co_await cleanupExpiredFriendRequests(*pid)) {
        logger->log(Logger::level::WARN, logGroup, "Failed to cleanup expired friend requests for " + std::to_string(*pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    auto blacklistCmd = db::Database::craftGetBlockedFriendsCommand(client.pid);
    const db::Result blacklistResult = co_await db->runCommand(std::move(blacklistCmd));
    if (blacklistResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get blocked friends for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (auto& blockedData : blacklistResult.getData<std::vector<db::DBBlockData>>()) {
        if (blockedData.blockedPid == *pid) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to add blacklisted friend "
                                                       + std::to_string(*pid));

            sendMsg(client, createError(req, Error::FPD__BLACKLISTED_BY_ME), {});
            co_return;
        }
    }

    auto sentFriendRequestsCmd = db::Database::craftGetSentFriendRequestsCommand(client.pid);
    const db::Result sentFriendRequestsResult = co_await db->runCommand(std::move(sentFriendRequestsCmd));
    if (sentFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get sent friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (auto& requestData : sentFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.toPid == *pid) {
            // Send the already sent friend request back to the client
            FriendRequest request(client.minorVersion);

            NNAInfo requestNNAInfo(client.minorVersion);
            requestNNAInfo.decode(std::move(*requestData.nnaInfo));
            FriendRequestMsg requestMsg(client.minorVersion);
            requestMsg.decode(std::move(requestData.data));

            request.principalBasicInfo = requestNNAInfo.info;
            request.friendRequestMsg = requestMsg;

            auto year2000 = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::from_time_t(946684800));
            if (requestMsg.expiresOn > year2000) {
                request.friendRequestMsg.id = requestData.id;
            }

            request.sentOn = Datetime(0, requestData.createdAt);

            FriendInfo friendInfo(client.minorVersion);

            Response res;
            res.protocolId = req.protocolId;
            res.extendedProtocolId = req.extendedProtocolId;
            res.methodId = req.methodId;
            res.callId = req.callId;
            res.success = true;

            std::vector<T_ptr> params(2);
            params[0] = std::make_unique<FriendRequest>(std::move(request));
            params[1] = std::make_unique<FriendInfo>(std::move(friendInfo));

            sendMsg(client, res, params);
            co_return;
        }
    }

    auto receivedFriendRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(client.pid);
    const db::Result receivedFriendRequestsResult = co_await db->runCommand(std::move(receivedFriendRequestsCmd));
    if (receivedFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    bool otherUserHadSentRequest = false;
    int64_t sentRequestId = -1;
    for (auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.toPid == *pid) {
            otherUserHadSentRequest = true;
            sentRequestId = requestData.id;
            break;
        }
    }

    auto friendInfoCmd = db::Database::craftGetUserInfoByPidCommand(*pid);
    const db::Result friendInfoResult = co_await db->runCommand(std::move(friendInfoCmd));
    if (friendInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(*pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (!friendInfoResult.hasData()) {
        logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(*pid) + " not found");

        sendMsg(client, createError(req, Error::FPD__INVALID_ACCOUNT), {});
        co_return;
    }

    auto friendBlockCmd = db::Database::craftGetBlockedFriendsCommand(*pid);
    const db::Result friendBlockResult = co_await db->runCommand(std::move(friendBlockCmd));
    if (friendBlockResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get blocked friends for " + std::to_string(*pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (auto& blockedData : friendBlockResult.getData<std::vector<db::DBBlockData>>()) {
        if (blockedData.blockedPid == client.pid) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to add blacklisted friend "
                                                       + std::to_string(*pid));

            // We tell the user that "the user can't receive any more requests"
            sendMsg(client, createError(req, Error::FPD__REQUEST_LIMIT_EXCEED), {});
            co_return;
        }
    }

    if (!otherUserHadSentRequest) {
        auto friendReceivedRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(*pid);
        const db::Result friendReceivedRequestsResult = co_await db->runCommand(std::move(friendReceivedRequestsCmd));
        if (friendReceivedRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(*pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        auto friendReceivedRequests = friendReceivedRequestsResult.getData<std::vector<db::DBFriendRequestData>>();
        if (friendReceivedRequests.size() >= 100) {
            logger->log(Logger::level::WARN, logGroup, "Friend " + std::to_string(*pid) + " has too many friend requests");

            sendMsg(client, createError(req, Error::FPD__REQUEST_LIMIT_EXCEED), {});
            co_return;
        }
    }
}

Task<void> FriendsSecureRMC::removeFriend(ClientInfo client, Request req, std::unique_ptr<PID> pid) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    bool hasFriend = false;
    for (auto& friendData : clientIt->second.friends) {
        if (friendData.pid == *pid) {
            hasFriend = true;
            break;
        }
    }
    registeredClientsLock.unlock();

    if (!hasFriend) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to remove non-friend "
                                                   + std::to_string(*pid));

        sendMsg(client, createError(req, Error::FPD__NOT_FRIEND), {});
        co_return;
    }

    auto deleteCmd = db::Database::craftDeleteFriendCommand(client.pid, *pid);
    const db::Result deleteResult = co_await db->runCommand(std::move(deleteCmd));
    if (deleteResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to delete friend " + std::to_string(*pid)
                                                   + " for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    sendMsg(client, res, {});
}

Task<void> FriendsSecureRMC::updatePresence(ClientInfo client, Request req, std::unique_ptr<NintendoPresenceV2> presence) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }
    registeredClientsLock.unlock();

    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(client.pid, std::nullopt,
                                                                      std::nullopt, std::nullopt,
                                                                      std::nullopt, std::nullopt,
                                                                      std::move(presence->encode()),
                                                                      std::nullopt, std::nullopt);
    const db::Result updateUserInfoResult = co_await db->runCommand(std::move(updateUserInfoCmd));

    if (updateUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    sendMsg(client, res, {});

    registeredClientsLock.lock();
    clientIt = registeredClients.find(client.pid);
    // Send presence update to connected friends
    for (auto& friendData : clientIt->second.friends) {
        AnyDataHolder data;
        presence->pid = client.pid;
        presence->online = true;
        data.set(*presence, "NintendoPresenceV2");

        auto friendIt = registeredClients.find(friendData.pid);
        if (friendIt == registeredClients.end()) continue;

        if (clientIt->second.userData.preference.showOnline && clientIt->second.userData.preference.showPlaying) {
            sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
        } else if (clientIt->second.userData.preference.showOnline) {
            NintendoPresenceV2 presenceUpdate(client.minorVersion);
            presenceUpdate.online = presence->online;
            data.set(*presence, "NintendoPresenceV2");
            sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
        }
    }
}

Task<void> FriendsSecureRMC::updateMii(ClientInfo client, Request req, std::unique_ptr<MiiV2> mii) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }
    registeredClientsLock.unlock();

    auto getUserInfoCmd = db::Database::craftGetUserInfoByPidCommand(client.pid);
    const db::Result getUserInfoResult = co_await db->runCommand(std::move(getUserInfoCmd));
    if (getUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (!getUserInfoResult.hasData()) {
        logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    auto userInfoData = getUserInfoResult.getData<db::DBUserInfoData>();
    NNAInfo nnaInfo(client.minorVersion);
    nnaInfo.decode(userInfoData.nnaInfo);

    nnaInfo.info.mii = *mii;
    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(client.pid, std::nullopt,
                                                                      std::nullopt, std::nullopt,
                                                                      std::nullopt, std::move(nnaInfo.encode()),
                                                                      std::nullopt, std::nullopt, std::nullopt);
    const db::Result updateUserInfoResult = co_await db->runCommand(std::move(updateUserInfoCmd));
    if (updateUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    // Unknown datetime, we just send the current time for now
    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<Datetime>(client.minorVersion, std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));

    sendMsg(client, res, params);

    // Send Mii change notification to connected friends
    registeredClientsLock.lock();
    clientIt = registeredClients.find(client.pid);

    AnyDataHolder notificationData;
    notificationData.set(nnaInfo, "NNAInfo");

    for (auto& friendData : clientIt->second.friends) {
        auto friendIt = registeredClients.find(friendData.pid);
        if (friendIt == registeredClients.end()) continue;

        sendNotification(friendIt->second.client, NintendoNotificationType::MII_CHANGED, client.pid, notificationData);
    }
}

Task<void> FriendsSecureRMC::updateComment(ClientInfo client, Request req, std::unique_ptr<Comment> comment) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }
    registeredClientsLock.unlock();

    db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    comment->lastModified = Datetime(client.minorVersion, now);

    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(
        client.pid, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, comment->encode(), std::nullopt);
    const db::Result updateUserInfoResult = co_await db->runCommand(std::move(updateUserInfoCmd));
    if (updateUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<Datetime>(client.minorVersion, now);

    sendMsg(client, res, params);

    // Send comment update notification to connected friends
    registeredClientsLock.lock();
    clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) co_return;
    AnyDataHolder notificationData;
    NintendoNotificationEventGeneral generalEvent(0);
    generalEvent.u32_param = comment->unk1;
    generalEvent.u64_param2.decode(Datetime(0, now).encode());
    generalEvent.str_param = comment->message;
    notificationData.set(generalEvent, "NintendoNotificationEventGeneral");

    for (auto& friendData : clientIt->second.friends) {
        auto friendIt = registeredClients.find(friendData.pid);
        if (friendIt == registeredClients.end()) continue;

        sendNotification(friendIt->second.client, NintendoNotificationType::COMMENT_CHANGED, client.pid, notificationData);
    }
}

Task<void> FriendsSecureRMC::updatePreference(ClientInfo client, Request req, std::unique_ptr<PrincipalPreference> preference) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }
    bool wasOnline = clientIt->second.userData.preference.showOnline;
    registeredClientsLock.unlock();

    auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(
        client.pid, std::nullopt, preference->showOnline, preference->showPlaying, preference->blockFriendRequest,
        std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    const db::Result updateUserInfoResult = co_await db->runCommand(std::move(updateUserInfoCmd));
    if (updateUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    sendMsg(client, res, {});

    if (wasOnline && !preference->showOnline) {
        // Send offline notification to connected friends
        registeredClientsLock.lock();
        clientIt = registeredClients.find(client.pid);
        if (clientIt == registeredClients.end()) co_return;

        NintendoNotificationEventGeneral generalEvent(0);
        generalEvent.u64_param2.decode(Datetime(0,
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())).encode());

        for (auto& friendData : clientIt->second.friends) {
            AnyDataHolder data;
            data.set(generalEvent, "NintendoNotificationEventGeneral");

            auto friendIt = registeredClients.find(friendData.pid);
            if (friendIt == registeredClients.end()) continue;

            sendNotification(friendIt->second.client, NintendoNotificationType::WENT_OFFLINE, clientIt->second.client.pid, data);
        }
    }
}

Task<void> FriendsSecureRMC::getBasicInfo(ClientInfo client, Request req, std::unique_ptr<List<PID>> pids) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    if (pids->size() > 100) {
        logger->log(Logger::level::WARN, logGroup, "Too many PIDs requested for get basic info: "
                                                   + std::to_string(pids->size()) + " from "
                                                   + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::FPD__OPERATION_NOT_ALLOWED), {});
        co_return;
    }

    auto infos = std::make_unique<List<PrincipalBasicInfo>>(client.minorVersion);
    for (const auto& pid : *pids) {
        auto getBlockSettingCmd = db::Database::craftGetUserInfoByPidCommand(pid);
        const db::Result getBlockSettingResult = co_await db->runCommand(std::move(getBlockSettingCmd));
        if (getBlockSettingResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));

            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        if (getBlockSettingResult.hasData()) {
            auto userInfoData = getBlockSettingResult.getData<db::DBUserInfoData>();

            PrincipalBasicInfo info(client.minorVersion);
            NNAInfo nnaInfo(client.minorVersion);
            nnaInfo.decode(userInfoData.nnaInfo);
            infos->push_back(std::move(nnaInfo.info));
        }
    }

    std::vector<T_ptr> params(1);
    params[0] = std::move(infos);

    sendMsg(client, res, params);
}

Task<void> FriendsSecureRMC::checkSettingStatus(ClientInfo client, Request req) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<UInt8>(client.minorVersion, FRIENDS_SETTING_STATUS);

    sendMsg(client, res, params);
    co_return;
}

Task<void> FriendsSecureRMC::getRequestBlockSettings(ClientInfo client, Request req, std::unique_ptr<List<PID>> pids) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    if (pids->size() > 100) {
        logger->log(Logger::level::WARN, logGroup, "Too many PIDs requested for request block settings: "
                                                   + std::to_string(pids->size()) + " from "
                                                   + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::FPD__OPERATION_NOT_ALLOWED), {});
        co_return;
    }

    auto settings = std::make_unique<List<PrincipalRequestBlockSetting>>(client.minorVersion);
    for (const auto& pid : *pids) {
        PrincipalRequestBlockSetting setting(client.minorVersion);
        setting.pid = pid;

        auto getBlockSettingCmd = db::Database::craftGetUserInfoByPidCommand(pid);
        const db::Result getBlockSettingResult = co_await db->runCommand(std::move(getBlockSettingCmd));
        if (getBlockSettingResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));

            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        if (getBlockSettingResult.hasData()) {
            auto userInfoData = getBlockSettingResult.getData<db::DBUserInfoData>();
            setting.blocked = userInfoData.blockRequests;
            settings->push_back(std::move(setting));
        }
    }

    std::vector<T_ptr> params(1);
    params[0] = std::move(settings);

    sendMsg(client, res, params);
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

Task<bool> FriendsSecureRMC::cleanupExpiredFriendRequests(const uint32_t pid) const {
    auto sentFriendRequestsCmd = db::Database::craftGetSentFriendRequestsCommand(pid);
    db::Result sentFriendRequestsResult = co_await db->runCommand(std::move(sentFriendRequestsCmd));
    if (sentFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get sent friend requests for " + std::to_string(pid) + ".");
        co_return false;
    }

    auto receivedFriendRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(pid);
    db::Result receivedFriendRequestsResult = co_await db->runCommand(std::move(receivedFriendRequestsCmd));
    if (receivedFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(pid) + ".");
        co_return false;
    }

    auto sentRequests = sentFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>();
    auto receivedRequests = receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>();

    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to begin transaction for cleaning up expired friend requests for " + std::to_string(pid) + ".");
        co_return false;
    }

    // If a request is created with an expiration of 0, it is considered permanent and should not be deleted.
    // We assume that all requests created before the year 2000 are permanent.
    auto year2000 = std::chrono::system_clock::from_time_t(946684800); // January 1, 2000

    for (const auto& request : sentRequests) {
        if (request.expiresAt < std::chrono::system_clock::now() && request.expiresAt > year2000) {
            auto deleteCmd = db::Database::craftDeleteFriendRequestCommand(request.id);
            db::Result deleteResult = co_await session->runCommand(std::move(deleteCmd));
            if (deleteResult.getStatus() != db::DBResultStatus::SUCCESS) {
                co_await session->rollbackTransaction();
                logger->log(Logger::level::WARN, logGroup, "Failed to delete expired sent friend request for " + std::to_string(pid) + ".");
                co_return false;
            }
        }
    }

    for (const auto& request : receivedRequests) {
        if (request.expiresAt < std::chrono::system_clock::now() && request.expiresAt > year2000) {
            auto deleteCmd = db::Database::craftDeleteFriendRequestCommand(request.id);
            db::Result deleteResult = co_await session->runCommand(std::move(deleteCmd));
            if (deleteResult.getStatus() != db::DBResultStatus::SUCCESS) {
                co_await session->rollbackTransaction();
                logger->log(Logger::level::WARN, logGroup, "Failed to delete expired sent friend request for " + std::to_string(pid) + ".");
                co_return false;
            }
        }
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for cleaning up expired friend requests for " + std::to_string(pid) + ".");
        co_return false;
    }

    co_return true;
}

Task<void> FriendsSecureRMC::onDisconnect(prudp::PRUDPAddress address) {
    const NintendoPresenceV2 presence(0);

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(pidMap[address]);
    if (clientIt != registeredClients.end()) {
        db::datetime_t lastOnline = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
        auto updateUserInfoCmd = db::Database::craftUpdateUserInfoCommand(clientIt->second.client.pid, std::nullopt,
                                                                          std::nullopt, std::nullopt,
                                                                          std::nullopt, std::nullopt,
                                                                          std::move(presence.encode()),
                                                                          std::nullopt, lastOnline);
        db::Result updateUserInfoResult = co_await db->runCommand(std::move(updateUserInfoCmd));

        if (updateUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to update user info for " + std::to_string(clientIt->second.client.pid)
                                                       + " from " + util::ipv4ToString(clientIt->second.client.address.address) + ":"
                                                       + std::to_string(clientIt->second.client.address.address.port));
        }

        if (clientIt->second.userData.preference.showOnline) {
            // Send presence update to connected friends
            NintendoNotificationEventGeneral generalEvent(0);
            generalEvent.u64_param2.decode(Datetime(0, lastOnline).encode());

            for (auto& friendData : clientIt->second.friends) {
                AnyDataHolder data;
                data.set(generalEvent, "NintendoNotificationEventGeneral");

                auto friendIt = registeredClients.find(friendData.pid);
                if (friendIt == registeredClients.end()) continue;


                sendNotification(friendIt->second.client, NintendoNotificationType::WENT_OFFLINE, clientIt->second.client.pid, data);
            }
        }

        registeredClients.erase(clientIt);
    }
    registeredClientsLock.unlock();

    co_await Server::onDisconnect(address);
}

} // namespace nex::rmc