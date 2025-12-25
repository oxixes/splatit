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
    REGISTER_CALL(FriendsSecureRMC::addFriendByName, 102, 3);
    REGISTER_CALL(FriendsSecureRMC::removeFriend, 102, 4);
    REGISTER_CALL(FriendsSecureRMC::addFriendRequest, 102, 5);
    REGISTER_CALL(FriendsSecureRMC::cancelFriendRequest, 102, 6);
    REGISTER_CALL(FriendsSecureRMC::acceptFriendRequest, 102, 7);
    REGISTER_CALL(FriendsSecureRMC::deleteFriendRequest, 102, 8);
    REGISTER_CALL(FriendsSecureRMC::denyFriendRequest, 102, 9);
    REGISTER_CALL(FriendsSecureRMC::markFriendRequestsAsReceived, 102, 10);
    REGISTER_CALL(FriendsSecureRMC::addBlackList, 102, 11);
    REGISTER_CALL(FriendsSecureRMC::removeBlackList, 102, 12);
    REGISTER_CALL(FriendsSecureRMC::updatePresence, 102, 13);
    REGISTER_CALL(FriendsSecureRMC::updateMii, 102, 14);
    REGISTER_CALL(FriendsSecureRMC::updateComment, 102, 15);
    REGISTER_CALL(FriendsSecureRMC::updatePreference, 102, 16);
    REGISTER_CALL(FriendsSecureRMC::getBasicInfo, 102, 17);
    REGISTER_CALL(FriendsSecureRMC::deletePersistentNotification, 102, 18);
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
        clientInfo.friends.insert(friendData.friendPid);
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

        if (requestData.expiresAt > year2000)
            request.friendRequestMsg.id = requestData.id;
        if (request.friendRequestMsg.id == static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF)) {
            // Don't show the user that the request was rejected
            request.friendRequestMsg.id = 0;
        }
        request.sentOn = Datetime(0, requestData.createdAt);

        request.friendRequestMsg.isReceived = 0; // Don't show the user that the request was received by the other user

        sentFriendRequests->push_back(std::move(request));
    }

    std::unique_ptr<List<FriendRequest>> receivedFriendRequests = std::make_unique<List<FriendRequest>>(client.minorVersion);
    for (auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        FriendRequest request(client.minorVersion);

        NNAInfo reqNNAInfo(client.minorVersion);
        reqNNAInfo.decode(std::move(*requestData.nnaInfo));

        FriendRequestMsg reqMsg(client.minorVersion);
        reqMsg.decode(std::move(requestData.data));

        if (requestData.expiresAt <= year2000) continue;
        if (reqMsg.id == static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF)) continue; // Don't show the user requests that were rejected

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
        for (auto& friendPid : clientIt->second.friends) {
            AnyDataHolder data;
            presence->pid = client.pid;
            presence->online = true;
            data.set(*presence, "NintendoPresenceV2");

            auto friendIt = registeredClients.find(friendPid);
            if (friendIt == registeredClients.end()) continue;

            if (clientIt->second.userData.preference.showOnline && clientIt->second.userData.preference.showPlaying) {
                sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, client.pid, data);
            } else if (clientIt->second.userData.preference.showOnline) {
                NintendoPresenceV2 presenceUpdate(client.minorVersion);
                presenceUpdate.online = presence->online;
                presenceUpdate.pid = client.pid;
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
    co_await addFriendInternal(client, req, std::move(pid), nullptr);
}

Task<void> FriendsSecureRMC::addFriendInternal(ClientInfo client, Request req, std::unique_ptr<PID> pid, std::unique_ptr<FriendRequestMsg> message) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    if (client.pid == *pid) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to add itself as a friend");

        sendMsg(client, createError(req, Error::FPD__INCOMPATIBLE_ACCOUNT), {});
        co_return;
    }

    std::unique_lock registeredClientsLock(registeredClientsMutex);
    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " not registered");

        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    uint32_t numFriends = clientIt->second.friends.size();

    for (auto& friendPid : clientIt->second.friends) {
        if (friendPid == *pid) {
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
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to add already sent friend request "
                                                       + std::to_string(*pid));
            sendMsg(client, createError(req, Error::FPD__FRIEND_ALREADY_EXISTS), {});
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

    uint32_t totalFriends = numFriends + sentFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>().size();
    auto year2000 = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::from_time_t(946684800));
    for (auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        FriendRequestMsg reqMsg(client.minorVersion);
        reqMsg.decode(std::move(requestData.data));

        if (reqMsg.id != static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF) && requestData.expiresAt > year2000) {
            totalFriends++;
        }
    }

    if (totalFriends >= 100) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to add friend "
                                                   + std::to_string(*pid) + ", but has too many friends");

        sendMsg(client, createError(req, Error::FPD__MY_FRIEND_LIST_LIMIT_EXCEED), {});
        co_return;
    }

    bool otherUserHadSentRequest = false;
    int64_t recvRequestId = -1;
    for (auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.fromPid == *pid) {
            otherUserHadSentRequest = true;
            recvRequestId = requestData.id;
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

    if (message != nullptr) {
        // Check that the friend does not have more than 100 friends
        auto getFriendFriendsCmd = db::Database::craftGetFriendsInfoCommand(*pid);
        const db::Result getFriendFriendsResult = co_await db->runCommand(std::move(getFriendFriendsCmd));
        if (getFriendFriendsResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get friends info for " + std::to_string(*pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        auto getFriendSentRequestsCmd = db::Database::craftGetSentFriendRequestsCommand(*pid);
        const db::Result getFriendSentRequestsResult = co_await db->runCommand(std::move(getFriendSentRequestsCmd));
        if (getFriendSentRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get sent friend requests for " + std::to_string(*pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        auto getFriendReceivedRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(*pid);
        const db::Result getFriendReceivedRequestsResult = co_await db->runCommand(std::move(getFriendReceivedRequestsCmd));
        if (getFriendReceivedRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(*pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        uint32_t totalFriendCount = getFriendFriendsResult.getData<std::vector<db::DBFriendInfoData>>().size()
                                    + getFriendSentRequestsResult.getData<std::vector<db::DBFriendRequestData>>().size();
        for (auto& requestData : getFriendReceivedRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
            FriendRequestMsg reqMsg(client.minorVersion);
            reqMsg.decode(std::move(requestData.data));

            if (reqMsg.id != static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF) && requestData.expiresAt > year2000) {
                totalFriendCount++;
            }
        }

        if (totalFriendCount >= 100) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(*pid) + " tried to add friend "
                                                       + std::to_string(client.pid) + ", but has too many friends");

            sendMsg(client, createError(req, Error::FPD__REQUEST_LIMIT_EXCEED), {});
            co_return;
        }
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

    bool rejectRequest = false;
    for (auto& blockedData : friendBlockResult.getData<std::vector<db::DBBlockData>>()) {
        if (blockedData.blockedPid == client.pid) {
            rejectRequest = true;
        }
    }

    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to begin transaction for adding friend " + std::to_string(*pid)
                                                   + " for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (otherUserHadSentRequest && !rejectRequest) {
        auto deleteReqCmd = db::Database::craftDeleteFriendRequestCommand(recvRequestId);
        const db::Result deleteReqResult = co_await session->runCommand(std::move(deleteReqCmd));
        if (deleteReqResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to delete friend request " + std::to_string(recvRequestId)
                                                       + " for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
        auto friendshipCmd = db::Database::craftAddFriendCommand(client.pid, *pid, now);
        const db::Result friendshipResult = co_await session->runCommand(std::move(friendshipCmd));
        if (friendshipResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to add friend " + std::to_string(*pid)
                                                       + " for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for adding friend " + std::to_string(*pid)
                                                       + " for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        co_await createBecameFriendsPersistentNotification(client.pid, *pid);

        FriendInfo friendInfo(client.minorVersion);
        friendInfo.nnaInfo.decode(friendInfoResult.getData<db::DBUserInfoData>().nnaInfo);
        friendInfo.presence.decode(friendInfoResult.getData<db::DBUserInfoData>().presence);
        friendInfo.comment.decode(friendInfoResult.getData<db::DBUserInfoData>().comment);
        friendInfo.lastOnline = Datetime(0, friendInfoResult.getData<db::DBUserInfoData>().lastOnline);
        friendInfo.becameFriends = Datetime(0, now);
        friendInfo.unk1 = 0;

        FriendRequest friendRequest(client.minorVersion);

        Response res;
        res.protocolId = req.protocolId;
        res.extendedProtocolId = req.extendedProtocolId;
        res.methodId = req.methodId;
        res.callId = req.callId;
        res.success = true;

        std::vector<T_ptr> params(2);
        params[0] = std::make_unique<FriendRequest>(std::move(friendRequest));
        params[1] = std::make_unique<FriendInfo>(std::move(friendInfo));

        registeredClientsLock.lock();
        clientIt = registeredClients.find(client.pid);
        if (clientIt != registeredClients.end()) {
            clientIt->second.friends.insert(*pid);
        }
        registeredClientsLock.unlock();

        sendMsg(client, res, params);

        registeredClientsLock.lock();
        auto friendIt = registeredClients.find(*pid);
        if (friendIt != registeredClients.end()) {
            friendIt->second.friends.insert(client.pid);

            // Send friend added notification to the other user
            AnyDataHolder data;

            auto selfDataRequest = db::Database::craftGetUserInfoByPidCommand(client.pid);
            const db::Result selfDataResult = co_await db->runCommand(std::move(selfDataRequest));
            if (selfDataResult.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));
                co_return;
            }

            if (!selfDataResult.hasData()) {
                logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");
                co_return;
            }

            auto selfData = selfDataResult.getData<db::DBUserInfoData>();

            FriendInfo notificationFriendInfo(client.minorVersion);
            notificationFriendInfo.nnaInfo.decode(std::move(selfData.nnaInfo));
            notificationFriendInfo.presence.decode(std::move(selfData.presence));
            notificationFriendInfo.comment.decode(std::move(selfData.comment));
            notificationFriendInfo.lastOnline = Datetime(0, selfData.lastOnline);
            notificationFriendInfo.becameFriends = Datetime(0, now);
            notificationFriendInfo.unk1 = 0;

            data.set(notificationFriendInfo, "FriendInfo");

            sendNotification(friendIt->second.client, NintendoNotificationType::FRIEND_REQUEST_ACCEPTED, client.pid, data);
        }
    } else {
        if (message != nullptr && friendInfoResult.getData<db::DBUserInfoData>().blockRequests) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(*pid) + " has blocked friend requests from "
                                                       + std::to_string(client.pid) + ", user should have used addFriend, instead of addFriendRequest");
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::FPD__FRIEND_REQUEST_BLOCKED), {});
            co_return;
        }

        auto expiration = std::chrono::system_clock::from_time_t(0);
        bool messageProvided = message != nullptr;

        std::unique_ptr<FriendRequestMsg> requestMsg = std::move(message);
        if (requestMsg == nullptr) {
            requestMsg = std::make_unique<FriendRequestMsg>(client.minorVersion);
        } else {
            expiration = requestMsg->expiresOn.value;
        }

        auto expirationCasted = std::chrono::time_point_cast<std::chrono::seconds>(expiration);
        auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

        if (rejectRequest) requestMsg->id = 0xFFFFFFFFFFFFFFFF; // Reject request, so set ID to invalid value

        auto friendRequestInsertCmd = db::Database::craftInsertOrUpdateFriendRequestCommand(
            std::nullopt, client.pid, *pid, expirationCasted, now, requestMsg->encode());
        auto insertResult = co_await session->runCommand(std::move(friendRequestInsertCmd));
        if (insertResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to insert friend request for " + std::to_string(client.pid)
                                                       + " to " + std::to_string(*pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for adding friend request for "
                                                       + std::to_string(client.pid) + " to " + std::to_string(*pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        requestMsg->id = messageProvided ? insertResult.getData<int64_t>() : 0;

        FriendRequest friendRequest(client.minorVersion);

        auto friendData = friendInfoResult.getData<db::DBUserInfoData>();
        NNAInfo nnaInfo(client.minorVersion);
        nnaInfo.decode(std::move(friendData.nnaInfo));

        friendRequest.principalBasicInfo = nnaInfo.info;
        friendRequest.friendRequestMsg = *requestMsg;
        friendRequest.sentOn = Datetime(0, now);

        FriendInfo friendInfo(client.minorVersion);

        Response res;
        res.protocolId = req.protocolId;
        res.extendedProtocolId = req.extendedProtocolId;
        res.methodId = req.methodId;
        res.callId = req.callId;
        res.success = true;

        std::vector<T_ptr> params(2);
        params[0] = std::make_unique<FriendRequest>(std::move(friendRequest));
        params[1] = std::make_unique<FriendInfo>(std::move(friendInfo));

        sendMsg(client, res, params);

        if (messageProvided && !rejectRequest) {
            registeredClientsLock.lock();
            auto friendIt = registeredClients.find(*pid);
            if (friendIt != registeredClients.end()) {
                // Send friend request notification to the other user
                auto selfDataRequest = db::Database::craftGetUserInfoByPidCommand(client.pid);
                const db::Result selfDataResult = co_await db->runCommand(std::move(selfDataRequest));
                if (selfDataResult.getStatus() != db::DBResultStatus::SUCCESS) {
                    logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                               + " from " + util::ipv4ToString(client.address.address) + ":"
                                                               + std::to_string(client.address.address.port));
                    co_return;
                }

                if (!selfDataResult.hasData()) {
                    logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");
                    co_return;
                }

                NNAInfo dbUserData(client.minorVersion);
                dbUserData.decode(selfDataResult.getData<db::DBUserInfoData>().nnaInfo);

                FriendRequest notificationFriendRequest(client.minorVersion);
                notificationFriendRequest.principalBasicInfo = dbUserData.info;
                notificationFriendRequest.friendRequestMsg = *requestMsg;
                notificationFriendRequest.sentOn = Datetime(0, now);

                AnyDataHolder notificationFriendRequestData;
                notificationFriendRequestData.set(notificationFriendRequest, "FriendRequest");

                sendNotification(friendIt->second.client, NintendoNotificationType::FRIEND_REQUEST_RECEIVED,
                                 client.pid, notificationFriendRequestData);
            }
        }
    }
}


Task<void> FriendsSecureRMC::addFriendByName(ClientInfo client, Request req, std::unique_ptr<String> username) {
    if (client.pid == 100) { // Guest users are not allowed to register
        sendMsg(client, createError(req, Error::CORE__ACCESS_DENIED), {});
        co_return;
    }

    auto getUserInfoCmd = db::Database::craftGetUserInfoByUsernameCommand(*username);
    const db::Result getUserInfoResult = co_await db->runCommand(std::move(getUserInfoCmd));
    if (getUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::string(*username)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (!getUserInfoResult.hasData()) {
        logger->log(Logger::level::WARN, logGroup, "User info for " + std::string(*username) + " not found");

        sendMsg(client, createError(req, Error::FPD__INVALID_ACCOUNT), {});
        co_return;
    }

    co_await addFriend(client, req, std::make_unique<PID>(0, getUserInfoResult.getData<db::DBUserInfoData>().pid));
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
    for (auto& friendPid : clientIt->second.friends) {
        if (friendPid == *pid) {
            hasFriend = true;
            break;
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

    if (!hasFriend) {
        auto sentFriendRequestsCmd = db::Database::craftGetSentFriendRequestsCommand(client.pid);
        const db::Result sentFriendRequestsResult = co_await db->runCommand(std::move(sentFriendRequestsCmd));
        if (sentFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get sent friend requests for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
        }

        bool foundFriendRequest = false;
        int64_t friendRequestId = -1;
        db::datetime_t friendRequestExpiration;
        for (auto& requestData : sentFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
            if (requestData.toPid == *pid) {
                foundFriendRequest = true;
                friendRequestId = requestData.id;
                friendRequestExpiration = requestData.expiresAt;
                break;
            }
        }

        if (!foundFriendRequest) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to remove non-friend "
                                                   + std::to_string(*pid));

            sendMsg(client, createError(req, Error::FPD__NOT_FRIEND), {});
            co_return;
        }

        auto deleteFriendRequestCmd = db::Database::craftDeleteFriendRequestCommand(friendRequestId);
        const db::Result deleteFriendRequestResult = co_await db->runCommand(std::move(deleteFriendRequestCmd));
        if (deleteFriendRequestResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to delete friend request " + std::to_string(friendRequestId)
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

        auto year2000 = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::from_time_t(946684800));
        if (friendRequestExpiration > year2000) {
            registeredClientsLock.lock();
            auto formerFriendIt = registeredClients.find(*pid);
            if (formerFriendIt != registeredClients.end()) {
                // Send friend removed notification to the other user
                NintendoNotificationEventGeneral event(client.minorVersion);
                event.u32_param = *pid;
                event.u64_param1 = friendRequestId;

                auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
                event.u64_param2.decode(Datetime(0, now).encode());

                AnyDataHolder data;
                data.set(event, "NintendoNotificationEventGeneral");

                sendNotification(formerFriendIt->second.client, NintendoNotificationType::FRIEND_REMOVED, client.pid, data);
            }
            registeredClientsLock.unlock();
        }
    } else {
        auto getSelfNotificationsCmd = db::Database::craftGetPersistentNotificationsCommand(client.pid);
        const db::Result getSelfNotificationsResult = co_await db->runCommand(std::move(getSelfNotificationsCmd));
        if (getSelfNotificationsResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get persistent notifications for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        auto getFormerFriendNotificationsCmd = db::Database::craftGetPersistentNotificationsCommand(*pid);
        const db::Result getFormerFriendNotificationsResult = co_await db->runCommand(std::move(getFormerFriendNotificationsCmd));
        if (getFormerFriendNotificationsResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to persistent notifications info for " + std::to_string(*pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }

        auto session = db->createSession();
        if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to begin transaction for deleting friend " + std::to_string(*pid)
                                                       + " for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
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

        for (auto& notification : getSelfNotificationsResult.getData<std::vector<db::DBPersistentNotificationData>>()) {
            if (notification.value4 == 30 && notification.value3 == client.pid && notification.value2 == *pid) {
                // Remove the friend removed notification
                auto deleteNotificationCmd = db::Database::craftDeletePersistentNotificationCommand(notification.id);
                const db::Result deleteNotificationResult = co_await session->runCommand(std::move(deleteNotificationCmd));
                if (deleteNotificationResult.getStatus() != db::DBResultStatus::SUCCESS) {
                    logger->log(Logger::level::WARN, logGroup, "Failed to delete friend removed notification for "
                                                               + std::to_string(client.pid) + " from " + util::ipv4ToString(client.address.address)
                                                               + ":" + std::to_string(client.address.address.port));
                    co_await session->rollbackTransaction();
                    sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                    co_return;
                }
            }
        }

        for (auto& notification : getFormerFriendNotificationsResult.getData<std::vector<db::DBPersistentNotificationData>>()) {
            if (notification.value4 == 30 && notification.value2 == client.pid && notification.value3 == *pid) {
                // Remove the friend removed notification
                auto deleteNotificationCmd = db::Database::craftDeletePersistentNotificationCommand(notification.id);
                const db::Result deleteNotificationResult = co_await session->runCommand(std::move(deleteNotificationCmd));
                if (deleteNotificationResult.getStatus() != db::DBResultStatus::SUCCESS) {
                    logger->log(Logger::level::WARN, logGroup, "Failed to delete friend removed notification for "
                                                               + std::to_string(client.pid) + " from " + util::ipv4ToString(client.address.address)
                                                               + ":" + std::to_string(client.address.address.port));
                    co_await session->rollbackTransaction();
                    sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                    co_return;
                }
            }
        }

        if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for deleting friend " + std::to_string(*pid)
                                                       + " for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
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
        if (clientIt != registeredClients.end()) {
            clientIt->second.friends.erase(*pid);
        }

        auto formerFriendIt = registeredClients.find(*pid);
        if (formerFriendIt != registeredClients.end()) {
            // Remove friend from the client's friend list
            formerFriendIt->second.friends.erase(client.pid);

            // Send friend removed notification to the other user
            NintendoNotificationEventGeneral event(client.minorVersion);
            event.u32_param = *pid;

            auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            event.u64_param2.decode(Datetime(0, now).encode());

            AnyDataHolder data;
            data.set(event, "NintendoNotificationEventGeneral");

            sendNotification(formerFriendIt->second.client, NintendoNotificationType::FRIEND_REMOVED, client.pid, data);
        }
        registeredClientsLock.unlock();
    }
}

Task<void> FriendsSecureRMC::addFriendRequest(ClientInfo client, Request req, std::unique_ptr<PID> pid,
                                              std::unique_ptr<UInt8> unk1, std::unique_ptr<String> message,
                                              std::unique_ptr<UInt8> unk2, std::unique_ptr<String> unk3,
                                              std::unique_ptr<GameKey> gameKey, std::unique_ptr<Datetime> unk4) {
    std::unique_ptr<FriendRequestMsg> msg = std::make_unique<FriendRequestMsg>(client.minorVersion);
    msg->unk1 = *unk1;
    msg->message = *message;
    msg->unk2 = *unk2;
    msg->unk3 = *unk3;
    msg->gameKey = *gameKey;
    msg->unk4 = *unk4;

    auto expiration = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())
        + std::chrono::days(30); // Friend requests expire after 30 days

    msg->expiresOn = Datetime(0, expiration);

    co_await addFriendInternal(client, req, std::move(pid), std::move(msg));
}

Task<void> FriendsSecureRMC::cancelFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id) {
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

    auto sentFriendRequestsCmd = db::Database::craftGetSentFriendRequestsCommand(client.pid);
    const db::Result sentFriendRequestsResult = co_await db->runCommand(std::move(sentFriendRequestsCmd));
    if (sentFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get sent friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (const auto& requestData : sentFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.id == static_cast<int64_t>(*id)) {
            auto deleteCmd = db::Database::craftDeleteFriendRequestCommand(requestData.id);
            const db::Result deleteResult = co_await db->runCommand(std::move(deleteCmd));
            if (deleteResult.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to delete friend request " + std::to_string(requestData.id)
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

            auto year2000 = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::from_time_t(946684800));
            FriendRequestMsg requestMsg(client.minorVersion);
            requestMsg.decode(requestData.data);

            if (requestData.expiresAt > year2000 && requestMsg.id != static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF)) {
                registeredClientsLock.lock();
                auto friendIt = registeredClients.find(requestData.toPid);
                if (friendIt != registeredClients.end()) {
                    NintendoNotificationEventGeneral event(client.minorVersion);
                    event.u32_param = requestData.toPid;
                    event.u64_param1 = requestData.id;

                    auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
                    event.u64_param2.decode(Datetime(0, now).encode());

                    AnyDataHolder data;
                    data.set(event, "NintendoNotificationEventGeneral");

                    sendNotification(friendIt->second.client, NintendoNotificationType::FRIEND_REMOVED, client.pid, data);
                }
                registeredClientsLock.unlock();
            }

            co_return;
        }
    }

    sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
}

Task<void> FriendsSecureRMC::acceptFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id) {
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

    auto receivedFriendRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(client.pid);
    const db::Result receivedFriendRequestsResult = co_await db->runCommand(std::move(receivedFriendRequestsCmd));
    if (receivedFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (const auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.id == static_cast<int64_t>(*id)) {
            FriendRequestMsg requestMsg(client.minorVersion);
            requestMsg.decode(requestData.data);

            if (requestMsg.id == static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF)) {
                logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to accept rejected friend request "
                                                           + std::to_string(requestData.id));

                sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
                co_return;
            }

            auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

            auto session = db->createSession();
            if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to begin transaction for accepting friend request "
                                                           + std::to_string(requestData.id) + " for " + std::to_string(client.pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));
                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                co_return;
            }

            auto deleteReqCmd = db::Database::craftDeleteFriendRequestCommand(requestData.id);
            const db::Result deleteReqResult = co_await session->runCommand(std::move(deleteReqCmd));
            if (deleteReqResult.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to delete friend request " + std::to_string(requestData.id)
                                                           + " for " + std::to_string(client.pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));
                co_await session->rollbackTransaction();
                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                co_return;
            }

            auto friendshipCmd = db::Database::craftAddFriendCommand(client.pid, requestData.fromPid, now);
            const db::Result friendshipResult = co_await session->runCommand(std::move(friendshipCmd));
            if (friendshipResult.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to add friend " + std::to_string(requestData.fromPid)
                                                           + " for " + std::to_string(client.pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));
                co_await session->rollbackTransaction();
                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                co_return;
            }

            auto friendInfoCmd = db::Database::craftGetUserInfoByPidCommand(requestData.fromPid);
            const db::Result friendInfoResult = co_await db->runCommand(std::move(friendInfoCmd));
            if (friendInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(requestData.fromPid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));
                co_await session->rollbackTransaction();
                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                co_return;
            }

            if (!friendInfoResult.hasData()) {
                logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(requestData.fromPid) + " not found");
                co_await session->rollbackTransaction();
                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                co_return;
            }

            if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for accepting friend request "
                                                           + std::to_string(requestData.id) + " for " + std::to_string(client.pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));
                co_await session->rollbackTransaction();
                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                co_return;
            }

            co_await createBecameFriendsPersistentNotification(client.pid, requestData.fromPid);

            Response res;
            res.protocolId = req.protocolId;
            res.extendedProtocolId = req.extendedProtocolId;
            res.methodId = req.methodId;
            res.callId = req.callId;
            res.success = true;

            FriendInfo friendInfo(client.minorVersion);
            friendInfo.nnaInfo.decode(friendInfoResult.getData<db::DBUserInfoData>().nnaInfo);
            friendInfo.presence.decode(friendInfoResult.getData<db::DBUserInfoData>().presence);
            friendInfo.comment.decode(friendInfoResult.getData<db::DBUserInfoData>().comment);
            friendInfo.lastOnline = Datetime(0, friendInfoResult.getData<db::DBUserInfoData>().lastOnline);
            friendInfo.becameFriends = Datetime(0, now);
            friendInfo.unk1 = 0;

            std::vector<T_ptr> params(1);
            params[0] = std::make_unique<FriendInfo>(std::move(friendInfo));

            registeredClientsLock.lock();
            clientIt = registeredClients.find(client.pid);
            if (clientIt != registeredClients.end()) {
                clientIt->second.friends.insert(requestData.fromPid);
            }
            registeredClientsLock.unlock();

            sendMsg(client, res, params);

            registeredClientsLock.lock();
            auto friendIt = registeredClients.find(requestData.fromPid);
            if (friendIt != registeredClients.end()) {
                friendIt->second.friends.insert(client.pid);

                auto selfDataRequest = db::Database::craftGetUserInfoByPidCommand(client.pid);
                const db::Result selfDataResult = co_await db->runCommand(std::move(selfDataRequest));
                if (selfDataResult.getStatus() != db::DBResultStatus::SUCCESS) {
                    logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                               + " from " + util::ipv4ToString(client.address.address) + ":"
                                                               + std::to_string(client.address.address.port));
                    co_return;
                }

                if (!selfDataResult.hasData()) {
                    logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");
                    co_return;
                }

                auto selfData = selfDataResult.getData<db::DBUserInfoData>();

                FriendInfo notificationFriendInfo(client.minorVersion);
                notificationFriendInfo.nnaInfo.decode(std::move(selfData.nnaInfo));
                notificationFriendInfo.presence.decode(std::move(selfData.presence));
                notificationFriendInfo.comment.decode(std::move(selfData.comment));
                notificationFriendInfo.lastOnline = Datetime(0, selfData.lastOnline);
                notificationFriendInfo.becameFriends = Datetime(0, now);
                notificationFriendInfo.unk1 = 0;

                AnyDataHolder data;
                data.set(notificationFriendInfo, "FriendInfo");

                sendNotification(friendIt->second.client, NintendoNotificationType::FRIEND_REQUEST_ACCEPTED, client.pid, data);
            }
            registeredClientsLock.unlock();
            co_return;
        }
    }

    sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
}

Task<void> FriendsSecureRMC::deleteFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id) {
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

    auto receivedFriendRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(client.pid);
    const db::Result receivedFriendRequestsResult = co_await db->runCommand(std::move(receivedFriendRequestsCmd));
    if (receivedFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (const auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.id == static_cast<int64_t>(*id)) {
            FriendRequestMsg requestMsg(client.minorVersion);
            requestMsg.decode(requestData.data);

            if (requestMsg.id == static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF)) {
                logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to reject already rejected friend request "
                                                           + std::to_string(requestData.id));

                sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
                co_return;
            }

            requestMsg.id = 0xFFFFFFFFFFFFFFFF; // Reject request, so set ID to invalid value

            auto rejectCmd = db::Database::craftInsertOrUpdateFriendRequestCommand(requestData.id,
                requestData.fromPid, requestData.toPid, requestData.expiresAt, requestData.createdAt, requestMsg.encode());
            const db::Result rejectResult = co_await db->runCommand(std::move(rejectCmd));
            if (rejectResult.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to reject friend request " + std::to_string(requestData.id)
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
            co_return;
        }
    }

    sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
}

Task<void> FriendsSecureRMC::denyFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id) {
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

    auto receivedFriendRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(client.pid);
    const db::Result receivedFriendRequestsResult = co_await db->runCommand(std::move(receivedFriendRequestsCmd));
    if (receivedFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (const auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.id == static_cast<int64_t>(*id)) {
            FriendRequestMsg requestMsg(client.minorVersion);
            requestMsg.decode(requestData.data);

            if (requestMsg.id == static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF)) {
                logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to deny already denied friend request "
                                                           + std::to_string(requestData.id));

                sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
                co_return;
            }

            std::unique_ptr<BlacklistedPrincipal> blacklist = std::make_unique<BlacklistedPrincipal>(client.minorVersion);
            blacklist->principalBasicInfo.pid = requestData.fromPid;

            // addBlacklist also rejects the request, so we can use it here
            co_await addBlackList(client, req, std::move(blacklist));
            co_return;
        }
    }

    sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
}

Task<void> FriendsSecureRMC::markFriendRequestsAsReceived(ClientInfo client, Request req, std::unique_ptr<List<UInt64>> requests) {
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

    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to begin transaction for marking friend requests as received for "
                                                   + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    auto getReceivedFriendRequestsCmd = db::Database::craftGetReceivedFriendRequestsCommand(client.pid);
    const db::Result getReceivedFriendRequestsResult = co_await session->runCommand(std::move(getReceivedFriendRequestsCmd));
    if (getReceivedFriendRequestsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get received friend requests for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        co_await session->rollbackTransaction();
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    std::unordered_map<int64_t, db::DBFriendRequestData> receivedRequests;
    for (const auto& requestData : getReceivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        receivedRequests[requestData.id] = requestData;
    }

    for (const auto& requestId : *requests) {
        auto it = receivedRequests.find(requestId);
        if (it == receivedRequests.end()) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to mark non-existing friend request "
                                                       + std::to_string(requestId) + " as received");

            sendMsg(client, createError(req, Error::FPD__INVALID_MESSAGE_ID), {});
            co_await session->rollbackTransaction();
            co_return;
        }

        // Mark the request as received
        FriendRequestMsg requestMsg(client.minorVersion);
        requestMsg.decode(it->second.data);

        requestMsg.isReceived = true;

        auto updateCmd = db::Database::craftInsertOrUpdateFriendRequestCommand(it->second.id,
                                                                                 it->second.fromPid, it->second.toPid,
                                                                                 it->second.expiresAt, it->second.createdAt,
                                                                                 requestMsg.encode());
        const db::Result updateResult = co_await session->runCommand(std::move(updateCmd));
        if (updateResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to update friend request " + std::to_string(it->second.id)
                                                       + " for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for marking friend requests as received for "
                                                   + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        co_await session->rollbackTransaction();
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

Task<void> FriendsSecureRMC::addBlackList(ClientInfo client, Request req, std::unique_ptr<BlacklistedPrincipal> blacklist) {
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

    for (const auto& friendPid : clientIt->second.friends) {
        if (friendPid == blacklist->principalBasicInfo.pid) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to block already existing friend "
                                                       + std::to_string(blacklist->principalBasicInfo.pid));

            sendMsg(client, createError(req, Error::FPD__FRIEND_LISTED_BY_ME), {});
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

    auto blockedUserInfoCmd = db::Database::craftGetUserInfoByPidCommand(blacklist->principalBasicInfo.pid);
    const db::Result blockedUserInfoResult = co_await db->runCommand(std::move(blockedUserInfoCmd));
    if (blockedUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(blacklist->principalBasicInfo.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        co_await db->rollbackTransaction();
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if (!blockedUserInfoResult.hasData()) {
        logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(blacklist->principalBasicInfo.pid) + " not found");

        sendMsg(client, createError(req, Error::FPD__INVALID_ACCOUNT), {});
        co_return;
    }

    auto blockedUserInfo = blockedUserInfoResult.getData<db::DBUserInfoData>();

    auto getBlockedFriendsCmd = db::Database::craftGetBlockedFriendsCommand(client.pid);
    const db::Result getBlockedFriendsResult = co_await db->runCommand(std::move(getBlockedFriendsCmd));
    if (getBlockedFriendsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get blocked friends for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (const auto& blockedData : getBlockedFriendsResult.getData<std::vector<db::DBBlockData>>()) {
        if (blockedData.blockedPid == blacklist->principalBasicInfo.pid) {
            BlacklistedPrincipal existingBlacklist(client.minorVersion);

            NNAInfo nnaInfo(client.minorVersion);
            nnaInfo.decode(blockedData.nnaInfo);

            existingBlacklist.principalBasicInfo = nnaInfo.info;
            existingBlacklist.gameKey.decode(blockedData.gameKey);
            existingBlacklist.blacklistedSince = Datetime(0, blockedData.createdAt);

            Response res;
            res.protocolId = req.protocolId;
            res.extendedProtocolId = req.extendedProtocolId;
            res.methodId = req.methodId;
            res.callId = req.callId;
            res.success = true;

            std::vector<T_ptr> params(1);
            params[0] = std::make_unique<BlacklistedPrincipal>(std::move(existingBlacklist));

            sendMsg(client, res, {});
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

    for (const auto& requestData : sentFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.toPid == blacklist->principalBasicInfo.pid) {
            logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to block already sent friend request "
                                                       + std::to_string(blacklist->principalBasicInfo.pid));

            sendMsg(client, createError(req, Error::FPD__FRIEND_LISTED_BY_ME), {});
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

    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to begin transaction for blocking friend "
                                                   + std::to_string(blacklist->principalBasicInfo.pid)
                                                   + " for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    for (const auto& requestData : receivedFriendRequestsResult.getData<std::vector<db::DBFriendRequestData>>()) {
        if (requestData.fromPid == blacklist->principalBasicInfo.pid) {
            FriendRequestMsg requestMsg(client.minorVersion);
            requestMsg.decode(requestData.data);
            requestMsg.id = 0xFFFFFFFFFFFFFFFF; // Reject request, so set ID to invalid value

            auto rejectRequestCmd = db::Database::craftInsertOrUpdateFriendRequestCommand(
                requestData.id, requestData.fromPid, requestData.toPid, requestData.expiresAt, requestData.createdAt,
                requestMsg.encode());
            const db::Result rejectRequestResult = co_await session->runCommand(std::move(rejectRequestCmd));
            if (rejectRequestResult.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to reject friend request " + std::to_string(requestData.id)
                                                           + " for " + std::to_string(client.pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));
                co_await session->rollbackTransaction();
                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                co_return;
            }
        }
    }

    auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    auto insertBlockCmd = db::Database::craftBlockFriendCommand(client.pid, blacklist->principalBasicInfo.pid,
                                                                now, blacklist->gameKey.encode());
    auto insertBlockResult = co_await session->runCommand(std::move(insertBlockCmd));
    if (insertBlockResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to block friend " + std::to_string(blacklist->principalBasicInfo.pid)
                                                   + " for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        co_await session->rollbackTransaction();
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for blocking friend "
                                                   + std::to_string(blacklist->principalBasicInfo.pid)
                                                   + " for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        co_await session->rollbackTransaction();
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    BlacklistedPrincipal blockedPrincipal(client.minorVersion);

    NNAInfo nnaInfo(client.minorVersion);
    nnaInfo.decode(blockedUserInfo.nnaInfo);

    blockedPrincipal.principalBasicInfo = nnaInfo.info;
    blockedPrincipal.gameKey = blacklist->gameKey;
    blockedPrincipal.blacklistedSince = Datetime(0, now);

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<BlacklistedPrincipal>(std::move(blockedPrincipal));

    sendMsg(client, res, params);
}

Task<void> FriendsSecureRMC::removeBlackList(ClientInfo client, Request req, std::unique_ptr<PID> pid) {
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

    auto getBlockedFriendsCmd = db::Database::craftGetBlockedFriendsCommand(client.pid);
    const db::Result getBlockedFriendsResult = co_await db->runCommand(std::move(getBlockedFriendsCmd));
    if (getBlockedFriendsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get blocked friends for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    bool wasBlocked = false;
    for (const auto& blockedData : getBlockedFriendsResult.getData<std::vector<db::DBBlockData>>()) {
        if (blockedData.blockedPid == *pid) {
            wasBlocked = true;
            break;
        }
    }

    if (!wasBlocked) {
        logger->log(Logger::level::WARN, logGroup, "Client " + std::to_string(client.pid) + " tried to remove non-blocked user "
                                                   + std::to_string(*pid));

        sendMsg(client, createError(req, Error::FPD__NOT_IN_MY_BLACKLIST), {});
        co_return;
    }

    auto deleteBlockCmd = db::Database::craftUnblockFriendCommand(client.pid, *pid);
    auto deleteBlockResult = co_await db->runCommand(std::move(deleteBlockCmd));
    if (deleteBlockResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to remove block for " + std::to_string(*pid)
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
    for (auto& friendPid : clientIt->second.friends) {
        AnyDataHolder data;
        presence->pid = client.pid;
        presence->online = true;
        data.set(*presence, "NintendoPresenceV2");

        auto friendIt = registeredClients.find(friendPid);
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

    for (auto& friendPid : clientIt->second.friends) {
        auto friendIt = registeredClients.find(friendPid);
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

    for (auto& friendPid : clientIt->second.friends) {
        auto friendIt = registeredClients.find(friendPid);
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

        for (auto& friendPid : clientIt->second.friends) {
            AnyDataHolder data;
            data.set(generalEvent, "NintendoNotificationEventGeneral");

            auto friendIt = registeredClients.find(friendPid);
            if (friendIt == registeredClients.end()) continue;

            sendNotification(friendIt->second.client, NintendoNotificationType::WENT_OFFLINE, clientIt->second.client.pid, data);
        }
    } else if (!wasOnline && preference->showOnline) {
        auto getUserInfoCmd = db::Database::craftGetUserInfoByPidCommand(client.pid);
        const db::Result getUserInfoResult = co_await db->runCommand(std::move(getUserInfoCmd));
        if (getUserInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get user info for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_return;
        }

        if (!getUserInfoResult.hasData()) {
            logger->log(Logger::level::WARN, logGroup, "User info for " + std::to_string(client.pid) + " not found");
            co_return;
        }

        NintendoPresenceV2 presence(client.minorVersion);
        if (preference->showPlaying) {
            presence.decode(getUserInfoResult.getData<db::DBUserInfoData>().presence);
        } else {
            presence.online = true; // If showOnline is true, we assume the user is online
            presence.pid = client.pid;
        }

        AnyDataHolder data;
        data.set(presence, "NintendoPresenceV2");

        // Send online notification to connected friends
        registeredClientsLock.lock();
        clientIt = registeredClients.find(client.pid);
        if (clientIt == registeredClients.end()) co_return;

        for (auto& friendPid : clientIt->second.friends) {
            auto friendIt = registeredClients.find(friendPid);
            if (friendIt == registeredClients.end()) continue;

            sendNotification(friendIt->second.client, NintendoNotificationType::PRESENCE_UPDATED, clientIt->second.client.pid, data);
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

Task<void> FriendsSecureRMC::deletePersistentNotification(ClientInfo client, Request req, std::unique_ptr<List<PersistentNotification>> notifications) {
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

    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to start transaction for deleting persistent notifications for "
                                                   + std::to_string(client.pid) + " from "
                                                   + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    auto getPersistentNotificationsCmd = db::Database::craftGetPersistentNotificationsCommand(client.pid);
    const db::Result getPersistentNotificationsResult = co_await session->runCommand(std::move(getPersistentNotificationsCmd));
    if (getPersistentNotificationsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get persistent notifications for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
        co_await session->rollbackTransaction();
        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    std::set<int64_t> notificationIdsToDelete;
    for (auto& existingNotification : getPersistentNotificationsResult.getData<std::vector<db::DBPersistentNotificationData>>()) {
        for (auto& notification : *notifications) {
            if (existingNotification.value2 == notification.unk2
                && existingNotification.value3 == notification.unk3
                && existingNotification.value4 == notification.unk4
                && existingNotification.text == std::string(notification.unk5)) {
                notificationIdsToDelete.insert(existingNotification.id);
                break;
            }
        }
    }

    if (notificationIdsToDelete.size() != notifications->size()) {
        logger->log(Logger::level::WARN, logGroup, "Some persistent notifications not found for " + std::to_string(client.pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        co_await session->rollbackTransaction();
        sendMsg(client, createError(req, Error::FPD__NOTIFICATION_NOT_FOUND), {});
        co_return;
    }

    for (const auto& notificationId : notificationIdsToDelete) {
        auto deleteNotificationCmd = db::Database::craftDeletePersistentNotificationCommand(notificationId);
        const db::Result deleteNotificationResult = co_await session->runCommand(std::move(deleteNotificationCmd));
        if (deleteNotificationResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup, "Failed to delete persistent notification " + std::to_string(notificationId)
                                                       + " for " + std::to_string(client.pid)
                                                       + " from " + util::ipv4ToString(client.address.address) + ":"
                                                       + std::to_string(client.address.address.port));
            co_await session->rollbackTransaction();
            sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
            co_return;
        }
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for deleting persistent notifications for "
                                                   + std::to_string(client.pid) + " from "
                                                   + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));
        co_await session->rollbackTransaction();
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
        co_await session->rollbackTransaction();
        co_return false;
    }

    co_return true;
}

Task<void> FriendsSecureRMC::createBecameFriendsPersistentNotification(const uint32_t pid, const uint32_t friendPid) const {
    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to begin transaction for persistent notification for " + std::to_string(pid) + ".");
        co_return;
    }

    auto insertNotificationCmd = db::Database::craftInsertOrUpdatePersistentNotificationCommand(
            std::nullopt, pid, 0, friendPid, pid, 30, ""
        );

    const db::Result insertNotificationResult = co_await session->runCommand(std::move(insertNotificationCmd));
    if (insertNotificationResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to insert persistent notification for " + std::to_string(pid) + ".");
        co_await session->rollbackTransaction();
        co_return;
    }

    const auto notificationId = insertNotificationResult.getData<int64_t>();
    auto updateNotificationCmd = db::Database::craftInsertOrUpdatePersistentNotificationCommand(
        notificationId, pid, notificationId, friendPid, pid, 30, ""
    );

    auto updateNotificationResult = co_await session->runCommand(std::move(updateNotificationCmd));
    if (updateNotificationResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update persistent notification for " + std::to_string(pid) + ".");
        co_await session->rollbackTransaction();
        co_return;
    }

    auto insertFriendNotificationCmd = db::Database::craftInsertOrUpdatePersistentNotificationCommand(
        std::nullopt, friendPid, 0, pid, friendPid, 30, ""
    );

    auto insertFriendNotificationResult = co_await session->runCommand(std::move(insertFriendNotificationCmd));
    if (insertFriendNotificationResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to insert persistent notification for friend " + std::to_string(friendPid) + ".");
        co_await session->rollbackTransaction();
        co_return;
    }

    const auto friendNotificationId = insertFriendNotificationResult.getData<int64_t>();
    auto updateFriendNotificationCmd = db::Database::craftInsertOrUpdatePersistentNotificationCommand(
        friendNotificationId, friendPid, friendNotificationId, pid, friendPid, 30, ""
    );

    auto updateFriendNotificationResult = co_await session->runCommand(std::move(updateFriendNotificationCmd));
    if (updateFriendNotificationResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update persistent notification for friend " + std::to_string(friendPid) + ".");
        co_await session->rollbackTransaction();
        co_return;
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to commit transaction for persistent notification for " + std::to_string(pid) + ".");
        co_await session->rollbackTransaction();
        co_return;
    }
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

            for (auto& friendPid : clientIt->second.friends) {
                AnyDataHolder data;
                data.set(generalEvent, "NintendoNotificationEventGeneral");

                auto friendIt = registeredClients.find(friendPid);
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