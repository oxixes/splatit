#include "auth.hpp"

#include "../prudp/kerberos.hpp"
#include "../types/common/string.hpp"
#include "../types/common/result.hpp"
#include "../types/common/buffer.hpp"
#include "../types/common/rvConnectionData.hpp"
#include "../types/auth/authenticationInfo.hpp"
#include "authUtils.hpp"

namespace nex::rmc {

using namespace async;

AuthRMC::AuthRMC(std::shared_ptr<Logger::Logger> logger, Logger::group logGroup, std::shared_ptr<db::Database> db,
                 sock::IPv4Addr secureAddr, std::string serverId, std::vector<uint8_t> secureServerKey,
                 std::string build, std::string base64JWTKey, bool friends) :
        Server(std::move(logger)), db(std::move(db)), secureAddr(secureAddr), serverId(std::move(serverId)),
        secureServerKey(std::move(secureServerKey)), build(std::move(build)), base64JWTKey(std::move(base64JWTKey)),
        friends(friends) {
    this->logGroup = logGroup;

    if (friends) REGISTER_CALL(AuthRMC::login, 10, 1);
    if (!friends) REGISTER_CALL(AuthRMC::loginEx, 10, 2);
    REGISTER_CALL(AuthRMC::requestTicket, 10, 3);
}

Task<void> AuthRMC::login(ClientInfo client, Request req, std::unique_ptr<String> username) {
    // Check if username is a number
    bool validUsername = std::ranges::all_of(*username, isdigit);

    // We convert the username, which is really the PID as a string, to an integer
    uint32_t pid = 0;
    if (validUsername) {
        try {
            pid = std::stoi(*username);
        } catch ([[maybe_unused]] const std::out_of_range& e) {
            validUsername = false;
        }
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.methodId = req.methodId;
    res.success = true; // The game expects a "successful" response with an error in the %retval% field

    if (!validUsername) {
        logger->log(Logger::level::INFO, logGroup, "Invalid username tried to log in: " + static_cast<std::string>(*username));

        std::vector<T_ptr> params(5);
        std::unique_ptr<Result> retval = std::make_unique<Result>();
        retval->code = Error::RENDEZ_VOUS__INVALID_USERNAME;
        retval->success = false;
        params[0] = std::move(retval);

        params[1] = std::make_unique<PID>();
        params[2] = std::make_unique<Buffer>();
        params[3] = std::make_unique<RVConnectionData>(client.minorVersion);
        params[4] = std::make_unique<String>();

        sendMsg(client, res, params);
    }

    auto dbCmd = db::Database::craftGetGameServerAccessCommand(pid);
    db::Result result = co_await spawn(scheduler, db->runCommand(std::move(dbCmd)));

    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user access for PID " + std::to_string(pid)
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    std::string userPasswd;
    if (result.hasData()) userPasswd = result.getData<db::DBGameServerAccessData>().password;

    validUsername = !userPasswd.empty();
    if (!validUsername) {
        logger->log(Logger::level::INFO, logGroup, "Invalid username tried to log in: " + static_cast<std::string>(*username));

        std::vector<T_ptr> params(5);
        auto retval = std::make_unique<Result>();
        retval->code = Error::RENDEZ_VOUS__INVALID_USERNAME;
        retval->success = false;
        params[0] = std::move(retval);

        params[1] = std::make_unique<PID>();
        params[2] = std::make_unique<Buffer>();
        params[3] = std::make_unique<RVConnectionData>(client.minorVersion);
        params[4] = std::make_unique<String>();

        sendMsg(client, res, params);
        co_return;
    }

    logger->log(Logger::level::INFO, logGroup, "User with PID " + std::to_string(pid) + " requested a ticket successfully.");

    // At this point, the user exists and we have the info we need to send them a ticket, so we do that
    std::vector<T_ptr> params(5);
    std::unique_ptr<Result> retval = std::make_unique<Result>();
    retval->code = Error::CORE__UNKNOWN; // This is the "error code" that the game uses when the ticket is valid
    retval->success = true;

    params[0] = std::move(retval);
    params[1] = std::make_unique<PID>(0, pid);

    std::vector<uint8_t> userPasswdVec(userPasswd.begin(), userPasswd.end());
    auto ticketData = prudp::kerberos::generateTicket(pid, userPasswdVec,
                                                      2, secureServerKey, friends);

    params[2] = std::make_unique<Buffer>(std::move(ticketData));

    StationURL secureUrl;
    secureUrl.proto = Protocol::PRUDPS;
    secureUrl.ip = secureAddr;
    secureUrl.port = secureAddr.port;
    secureUrl.stream = 0xA;
    secureUrl.sid = 1;
    secureUrl.CID = 1;
    secureUrl.type = 2;
    secureUrl.PID = 2;

    std::unique_ptr<RVConnectionData> connectionData = std::make_unique<RVConnectionData>(client.minorVersion);
    connectionData->urlRegularProtocols = std::move(secureUrl);

    params[3] = std::move(connectionData);
    params[4] = std::make_unique<String>(build);

    sendMsg(client, res, params);
}

Task<void> AuthRMC::loginEx(ClientInfo client, Request req, std::unique_ptr<String> username, std::unique_ptr<AnyDataHolder> authInfo) {
    if (authInfo->getType() != "AuthenticationInfo") {
        logger->log(Logger::level::WARN, logGroup, "Invalid data type for loginEx: " + authInfo->getType()
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        co_return;
    }

    AuthenticationInfo info(client.minorVersion);
    try {
        info = authInfo->get<AuthenticationInfo>();
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Malformed AuthenticationInfo in loginEx: " + std::string(e.what())
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        co_return;
    }

    // Check if username is a number
    // We convert the username, which is really the PID as a string, to an integer
    if (std::ranges::all_of(*username, isdigit)) {
        try {
            client.pid = std::stoi(*username);
        } catch ([[maybe_unused]] const std::out_of_range& e) {}
    }

    std::string token = info.authToken;
    if (!utils::checkJWT(token, base64JWTKey, serverId, client, logger, logGroup)) {
        logger->log(Logger::level::INFO, logGroup, "Invalid JWT token from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        Response res;
        res.protocolId = req.protocolId;
        res.extendedProtocolId = req.extendedProtocolId;
        res.callId = req.callId;
        res.methodId = req.methodId;
        res.success = true; // The game expects a "successful" response with an error in the %retval% field

        std::vector<T_ptr> params(5);
        auto retval = std::make_unique<Result>();
        retval->code = Error::CORE__ACCESS_DENIED;
        retval->success = false;

        params[0] = std::move(retval);
        params[1] = std::make_unique<PID>();
        params[2] = std::make_unique<Buffer>();
        params[3] = std::make_unique<RVConnectionData>(client.minorVersion);
        params[4] = std::make_unique<String>();

        sendMsg(client, res, params);
        co_return;
    }

    // Since the token is valid, we can just call login
    co_await login(client, req, std::move(username));
}

Task<void> AuthRMC::requestTicket(ClientInfo client, Request req, std::unique_ptr<PID> idSource, std::unique_ptr<PID> idTarget) {
    bool validPid = true;
    if (*idTarget != static_cast<uint32_t>(2)) validPid = false; // 2 is the PID of the secure server

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.methodId = req.methodId;
    res.success = true; // The game expects a "successful" response with an error in the %retval% field

    if (!validPid) {
        logger->log(Logger::level::INFO, logGroup, "Invalid PID tried to request a ticket, source: "
                                                   + std::to_string(*idSource) + ", target: " + std::to_string(*idTarget));

        std::vector<T_ptr> params(2);
        std::unique_ptr<Result> retval = std::make_unique<Result>();
        retval->code = Error::CORE__ACCESS_DENIED;
        retval->success = false;
        params[0] = std::move(retval);
        params[1] = std::make_unique<Buffer>();

        sendMsg(client, res, params);
        co_return;
    }

    auto dbCmd = db::Database::craftGetGameServerAccessCommand(*idSource);
    db::Result result = co_await spawn(scheduler, db->runCommand(std::move(dbCmd)));

    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to get user access for PID " + std::to_string(*idSource)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
        co_return;
    }

    std::string userPasswd;
    if (result.hasData()) userPasswd = result.getData<db::DBGameServerAccessData>().password;

    validPid = !userPasswd.empty();
    if (!validPid) {
        logger->log(Logger::level::INFO, logGroup, "Invalid PID tried to request a ticket: "
                                                   + std::to_string(*idSource) + ", target: " + std::to_string(*idTarget));

        std::vector<T_ptr> params(2);
        auto retval = std::make_unique<Result>();
        retval->code = Error::CORE__ACCESS_DENIED;
        retval->success = false;
        params[0] = std::move(retval);
        params[1] = std::make_unique<Buffer>();

        sendMsg(client, res, params);
        co_return;
    }

    logger->log(Logger::level::INFO, logGroup, "User with PID " + std::to_string(*idSource) + " requested a ticket successfully.");

    // At this point, the user exists and we have the info we need to send them a ticket, so we do that
    std::vector<T_ptr> params(2);
    auto retval = std::make_unique<Result>();
    retval->code = Error::CORE__UNKNOWN; // This is the "error code" that the game uses when the ticket is valid
    retval->success = true;

    params[0] = std::move(retval);;

    std::vector<uint8_t> userPasswdVec(userPasswd.begin(), userPasswd.end());
    auto ticketData = prudp::kerberos::generateTicket(*idSource, userPasswdVec,
                                                      2, secureServerKey, friends);

    params[1] = std::make_unique<Buffer>(ticketData);

    sendMsg(client, res, params);
}

Task<std::optional<std::string>> AuthRMC::getOrRegisterUserPassword(uint32_t pid) const {
    auto dbCmd = db::Database::craftGetGameServerAccessCommand(pid);
    const db::Result result = co_await spawn(scheduler, db->runCommand(std::move(dbCmd)));

    if (result.getStatus() != db::DBResultStatus::SUCCESS) co_return std::nullopt;

    if (result.hasData()) {
        co_return std::make_optional(result.getData<db::DBGameServerAccessData>().password);
    }

    logger->log(Logger::level::INFO, logGroup, "Registering new user password for PID " + std::to_string(pid));

    // Generate a new password for the user
    const std::string newPassword = utils::generateUserPassword();
    auto insertCmd = db::Database::craftInsertGameServerAccessCommand(pid, newPassword);
    const db::Result insertResult = co_await spawn(scheduler, db->runCommand(std::move(insertCmd)));

    if (insertResult.getStatus() == db::DBResultStatus::SUCCESS) {
        co_return std::make_optional(newPassword);
    }

    co_return std::nullopt;
}

} // namespace nex::rmc