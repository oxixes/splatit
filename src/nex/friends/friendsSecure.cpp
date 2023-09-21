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

    auto jwtToken = data.get<String>();

    if (!checkJWT(jwtToken, client)) {
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

bool FriendsSecureRMC::checkJWT(const std::string& jwtToken, ClientInfo& client) {
    if (!crypto::verifyJWT(base64JWTKey, jwtToken)) {
        logger->log(Logger::level::WARN, logGroup, "Invalid JWT token from " + util::ipv4ToString(client.address.address)
                                                   + ":" + std::to_string(client.address.address.port));
        return false;
    }

    std::string jwtData = ((std::string) jwtToken).substr(((std::string) jwtToken).find('.') + 1);
    jwtData = jwtData.substr(0, jwtData.find('.'));

    auto jwtJson = nlohmann::json::parse(crypto::base64UrlDecode(jwtData));
    if (time(nullptr) > jwtJson["exp"].get<time_t>()) {
        logger->log(Logger::level::WARN, logGroup, "Expired JWT token from " + util::ipv4ToString(client.address.address)
                                                   + ":" + std::to_string(client.address.address.port));
        return false;
    }

    if (jwtJson["iss"].get<std::string>() != "account") {
        logger->log(Logger::level::WARN, logGroup, "Invalid JWT issuer from " + util::ipv4ToString(client.address.address)
                                                   + ":" + std::to_string(client.address.address.port));
        return false;
    }

    if (jwtJson["game_server_id"].get<std::string>() != FRIENDS_SERVER_ID) {
        logger->log(Logger::level::WARN, logGroup, "Invalid JWT server id from " + util::ipv4ToString(client.address.address)
                                                   + ":" + std::to_string(client.address.address.port));
        return false;
    }

    if (jwtJson["sub"].get<uint32_t>() != client.pid) {
        logger->log(Logger::level::WARN, logGroup, "Non-matching PID in JWT from " + util::ipv4ToString(client.address.address)
                                                   + ":" + std::to_string(client.address.address.port));
        return false;
    }

    return true;
}

} // namespace nex::rmc