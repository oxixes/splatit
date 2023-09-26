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

#include <utility>

namespace nex::rmc {

FriendsSecureRMC::FriendsSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                                   std::string base64JWTKey) :
        Server(std::move(logger)), db(std::move(db)), base64JWTKey(std::move(base64JWTKey)) {
    logGroup = Logger::group::FRIENDS_SECURE;

    // Protocol 11 - Secure connection
    registerCall(this, &FriendsSecureRMC::registerEx, 11, 4);

    // Protocol 102 - Friends (Wii U)
    registerCall(this, &FriendsSecureRMC::updateAndGetAllInformation, 102, 1);
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
        clientPublicUrl.empty = true;

        params[0] = std::make_shared<Result>(retval);
        params[1] = std::make_shared<UInt32>(0, rvConnId);
        params[2] = std::make_shared<StationURL>(clientPublicUrl);

        sendMsg(client, res, params);
        return;
    }

    logger->log(Logger::level::INFO, logGroup, "Registered " + std::to_string(client.pid) + " from "
                            + util::ipv4ToString(client.address.address) + ":" + std::to_string(client.address.address.port));

    retval.code = Error::CORE__UNKNOWN;
    retval.success = true;
    rvConnId = nextRVConnId++;
    clientPublicUrl = urls[0];
    clientPublicUrl.ip = client.address.address;
    clientPublicUrl.port = client.address.address.port;

    // TODO Save registered URL(s)

    params[0] = std::make_shared<Result>(retval);
    params[1] = std::make_shared<UInt32>(0, rvConnId);
    params[2] = std::make_shared<StationURL>(clientPublicUrl);

    sendMsg(client, res, params);
}

void FriendsSecureRMC::updateAndGetAllInformation(ClientInfo client, Request req, NNAInfo nnaInfo) {
    // FIXME This is a stub

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = true;

    PrincipalPreference principalPreference(client.minorVersion);
    principalPreference.showOnline = true;
    principalPreference.showPlaying = true;
    principalPreference.blockFriendRequest = false;

    Comment statusMsg(client.minorVersion);
    statusMsg.message = "Hello, world!";

    List<FriendInfo> friendList(client.minorVersion);
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
}

} // namespace nex::rmc