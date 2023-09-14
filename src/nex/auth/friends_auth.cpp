#include "friends_auth.hpp"

#include "../../constants.hpp"
#include "../prudp/kerberos.hpp"
#include "../types/common/string.hpp"
#include "../types/common/result.hpp"
#include "../types/common/buffer.hpp"
#include "../types/common/rvConnectionData.hpp"

namespace nex::rmc {

FriendsAuthRMC::FriendsAuthRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                               sock::IPv4Addr secureAddr) :
        Server(std::move(logger)), db(std::move(db)), secureAddr(secureAddr) {
    logGroup = Logger::group::FRIENDS_AUTH;

    registerCall(this, &FriendsAuthRMC::login, 10, 1);
    registerCall(this, &FriendsAuthRMC::requestTicket, 10, 3);
}

void FriendsAuthRMC::login(nex::rmc::ClientInfo client, Request req, String username) {
    // Check if username is a number
    bool validUsername = std::all_of(username.begin(), username.end(), ::isdigit);

    // We convert the username, which is really the PID as a string, to an integer
    uint32_t pid;
    try {
        pid = std::stoi(username);
    } catch (const std::out_of_range& e) {
        validUsername = false;
    }

    std::string userPasswd;
    if (validUsername) userPasswd = getUserAccessPassword(pid);
    if (userPasswd.empty()) validUsername = false;

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.methodId = req.methodId;
    res.success = true; // The game expects a "successful" response with an error in the %retval% field

    if (!validUsername) {
        logger->log(Logger::level::INFO, logGroup, "Invalid username tried to log in: " + (std::string) username);

        std::vector<T_ptr> params(5);
        Result retval;
        retval.code = Error::RENDEZ_VOUS__INVALID_USERNAME;
        retval.success = false;
        params[0] = std::make_shared<Result>(retval);

        params[1] = std::make_shared<PID>();
        params[2] = std::make_shared<Buffer>();

        StationURL emptyUrl;
        emptyUrl.empty = true;
        RVConnectionData connectionData(client.minorVersion);
        connectionData.urlRegularProtocols = emptyUrl;
        connectionData.urlSpecialProtocols = emptyUrl;

        params[3] = std::make_shared<RVConnectionData>(connectionData);
        params[4] = std::make_shared<String>();

        sendMsg(client, res, params);
        return;
    }

    logger->log(Logger::level::INFO, logGroup, "User with PID " + std::to_string(pid) + " requested a ticket successfully.");

    // At this point, the user exists and we have the info we need to send them a ticket, so we do that
    std::vector<T_ptr> params(5);
    Result retval;
    retval.code = Error::CORE__UNKNOWN; // This is the "error code" that the game uses when the ticket is valid
    retval.success = true;

    params[0] = std::make_shared<Result>(retval);
    params[1] = std::make_shared<PID>(0, pid);

    std::vector<uint8_t> userPasswdVec(userPasswd.begin(), userPasswd.end());
    auto ticketData = prudp::kerberos::generateTicket(pid, userPasswdVec,
                                                      2, FRIENDS_SECURE_SERVER_KEY, true);

    params[2] = std::make_shared<Buffer>(ticketData);

    StationURL secureUrl;
    secureUrl.proto = Protocol::PRUDPS;
    secureUrl.ip = secureAddr;
    secureUrl.stream = 0xA;
    secureUrl.sid = 1;
    secureUrl.CID = 1;
    secureUrl.type = 2;
    secureUrl.PID = 2;

    StationURL emptyUrl;
    emptyUrl.empty = true;

    RVConnectionData connectionData(client.minorVersion);
    connectionData.urlRegularProtocols = std::move(secureUrl);
    connectionData.urlSpecialProtocols = std::move(emptyUrl);

    params[3] = std::make_shared<RVConnectionData>(connectionData);
    params[4] = std::make_shared<String>(BUILD);

    sendMsg(client, res, params);
}

void FriendsAuthRMC::requestTicket(ClientInfo client, Request req, PID idSource, PID idTarget) {
    bool validPid = true;
    if (idTarget != (uint32_t) 2) validPid = false; // 2 is the PID of the secure server

    std::string userPasswd;
    if (validPid) userPasswd = getUserAccessPassword(idSource);
    if (userPasswd.empty()) validPid = false;

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.methodId = req.methodId;
    res.success = true; // The game expects a "successful" response with an error in the %retval% field

    if (!validPid) {
        logger->log(Logger::level::INFO, logGroup, "Invalid PID tried to request a ticket, source: "
            + std::to_string(idSource) + ", target: " + std::to_string(idTarget));

        std::vector<T_ptr> params(2);
        Result retval;
        retval.code = Error::CORE__ACCESS_DENIED;
        retval.success = false;
        params[0] = std::make_shared<Result>(retval);
        params[1] = std::make_shared<Buffer>();

        sendMsg(client, res, params);
        return;
    }

    logger->log(Logger::level::INFO, logGroup, "User with PID " + std::to_string(idSource) + " requested a ticket successfully.");

    // At this point, the user exists and we have the info we need to send them a ticket, so we do that
    std::vector<T_ptr> params(2);
    Result retval;
    retval.code = Error::CORE__UNKNOWN; // This is the "error code" that the game uses when the ticket is valid
    retval.success = true;

    params[0] = std::make_shared<Result>(retval);

    std::vector<uint8_t> userPasswdVec(userPasswd.begin(), userPasswd.end());
    auto ticketData = prudp::kerberos::generateTicket(idSource, userPasswdVec,
                                                      2, FRIENDS_SECURE_SERVER_KEY, true);

    params[1] = std::make_shared<Buffer>(ticketData);

    sendMsg(client, res, params);
}

std::string FriendsAuthRMC::getUserAccessPassword(uint32_t pid) {
    auto dbCmd = db::Database::craftGetGameServerAccessCommand((int) pid, FRIENDS_SERVER_ID);
    uint32_t id = db::Database::runCommand(db, std::move(dbCmd), registerCloseCall, unregisterCloseCall, shouldStop);
    auto result = db->getResult(id);
    if (result->status != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");
    if (result->data.empty()) return "";
    return std::any_cast<std::string>(result->data[0]);
}

} // namespace nex::rmc