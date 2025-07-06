#include "auth.hpp"

#include "../prudp/kerberos.hpp"
#include "../types/common/string.hpp"
#include "../types/common/result.hpp"
#include "../types/common/buffer.hpp"
#include "../types/common/rvConnectionData.hpp"
#include "../types/auth/authenticationInfo.hpp"
#include "authUtils.hpp"
#include "../../constants.hpp"
#include "../types/friendsSecure/NNAInfo.hpp"
#include "../types/friendsSecure/nintendoPresenceV2.hpp"
#include "../types/friendsSecure/comment.hpp"

namespace nex::rmc {

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

void AuthRMC::login(ClientInfo client, Request req, std::unique_ptr<String> username) {
    // Check if username is a number
    bool validUsername = std::all_of(username->begin(), username->end(), ::isdigit);

    // We convert the username, which is really the PID as a string, to an integer
    uint32_t pid;
    if (validUsername) {
        try {
            pid = std::stoi(*username);
        } catch (const std::out_of_range& e) {
            validUsername = false;
        }
    }

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.methodId = req.methodId;
    res.success = true; // The game expects a "successful" response with an error in the %retval% field

    if (validUsername) {
        auto dbCmd = db::Database::craftGetGameServerAccessCommand(pid);
        db->runCommand(std::move(dbCmd), queueMutex, queueCV, promisesQueue)
            ->setContext(std::pair<ClientInfo, Request>(client, req))
            .then([username = std::move(username),
                    client, pid, req, res, this](std::any&& resultsAny) mutable {

            std::unique_ptr<db::Result> result = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

            if (result->getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to get user access for PID " + std::to_string(pid)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));

                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                return;
            }

            std::string userPasswd;
            if (result->hasData()) userPasswd = result->getData<db::DBGameServerAccessData>().password;

            bool validUsername = !userPasswd.empty();
            if (!validUsername) {
                logger->log(Logger::level::INFO, logGroup, "Invalid username tried to log in: " + (std::string) *username);

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
                return;
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
        });
    } else {
        logger->log(Logger::level::INFO, logGroup, "Invalid username tried to log in: " + (std::string) *username);

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
        return;
    }
}

void AuthRMC::loginEx(ClientInfo client, Request req, std::unique_ptr<String> username, std::unique_ptr<AnyDataHolder> authInfo) {
    if (authInfo->getType() != "AuthenticationInfo") {
        logger->log(Logger::level::WARN, logGroup, "Invalid data type for loginEx: " + authInfo->getType()
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        return;
    }

    AuthenticationInfo info(client.minorVersion);
    try {
        info = authInfo->get<AuthenticationInfo>();
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Malformed AuthenticationInfo in loginEx: " + std::string(e.what())
                                                   + " from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        sendMsg(client, createError(req, Error::CORE__INVALID_ARGUMENT), {});
        return;
    }

    // Check if username is a number
    bool validUsername = std::all_of(username->begin(), username->end(), ::isdigit);

    // We convert the username, which is really the PID as a string, to an integer
    if (validUsername) {
        try {
            client.pid = std::stoi(*username);
        } catch (const std::out_of_range& e) {}
    }

    std::string token = info.authToken;
    if (!utils::checkJWT(token, base64JWTKey, SPLATOON_SERVER_ID, client, logger, logGroup)) {
        logger->log(Logger::level::INFO, logGroup, "Invalid JWT token from " + util::ipv4ToString(client.address.address) + ":"
                                                   + std::to_string(client.address.address.port));

        Response res;
        res.protocolId = req.protocolId;
        res.extendedProtocolId = req.extendedProtocolId;
        res.callId = req.callId;
        res.methodId = req.methodId;
        res.success = true; // The game expects a "successful" response with an error in the %retval% field

        std::vector<T_ptr> params(5);
        std::unique_ptr<Result> retval = std::make_unique<Result>();
        retval->code = Error::CORE__ACCESS_DENIED;
        retval->success = false;

        params[0] = std::move(retval);
        params[1] = std::make_unique<PID>();
        params[2] = std::make_unique<Buffer>();
        params[3] = std::make_unique<RVConnectionData>(client.minorVersion);
        params[4] = std::make_unique<String>();

        sendMsg(client, res, params);
        return;
    }

    // Since the token is valid, we can just call login
    login(client, req, std::move(username));
}

void AuthRMC::requestTicket(ClientInfo client, Request req, std::unique_ptr<PID> idSource, std::unique_ptr<PID> idTarget) {
    bool validPid = true;
    if (*idTarget != (uint32_t) 2) validPid = false; // 2 is the PID of the secure server

    std::string userPasswd;

    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.methodId = req.methodId;
    res.success = true; // The game expects a "successful" response with an error in the %retval% field

    if (validPid) {
        auto dbCmd = db::Database::craftGetGameServerAccessCommand(*idSource);
        db->runCommand(std::move(dbCmd), queueMutex, queueCV, promisesQueue)
            ->setContext(std::pair<ClientInfo, Request>(client, req))
            .then([idSource = std::move(idSource),
                    idTarget = std::move(idTarget),
                    client, req, res, this](std::any&& resultsAny) mutable {

            std::unique_ptr<db::Result> result = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

            if (result->getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup, "Failed to get user access for PID " + std::to_string(*idSource)
                                                           + " from " + util::ipv4ToString(client.address.address) + ":"
                                                           + std::to_string(client.address.address.port));

                sendMsg(client, createError(req, Error::RENDEZ_VOUS__DATABASE_TEMPORARILY_UNAVAILABLE), {});
                return;
            }

            std::string userPasswd;
            if (result->hasData()) userPasswd = result->getData<db::DBGameServerAccessData>().password;

            bool validPid = !userPasswd.empty();
            if (!validPid) {
                logger->log(Logger::level::INFO, logGroup, "Invalid PID tried to request a ticket: "
                                                           + std::to_string(*idSource) + ", target: " + std::to_string(*idTarget));

                std::vector<T_ptr> params(2);
                std::unique_ptr<Result> retval = std::make_unique<Result>();
                retval->code = Error::CORE__ACCESS_DENIED;
                retval->success = false;
                params[0] = std::move(retval);
                params[1] = std::make_unique<Buffer>();

                sendMsg(client, res, params);
                return;
            }

            logger->log(Logger::level::INFO, logGroup, "User with PID " + std::to_string(*idSource) + " requested a ticket successfully.");

            // At this point, the user exists and we have the info we need to send them a ticket, so we do that
            std::vector<T_ptr> params(2);
            std::unique_ptr<Result> retval = std::make_unique<Result>();
            retval->code = Error::CORE__UNKNOWN; // This is the "error code" that the game uses when the ticket is valid
            retval->success = true;

            params[0] = std::move(retval);;

            std::vector<uint8_t> userPasswdVec(userPasswd.begin(), userPasswd.end());
            auto ticketData = prudp::kerberos::generateTicket(*idSource, userPasswdVec,
                                                              2, secureServerKey, friends);

            params[1] = std::make_unique<Buffer>(ticketData);

            sendMsg(client, res, params);
        });
    } else {
        logger->log(Logger::level::INFO, logGroup, "Invalid PID tried to request a ticket, source: "
                                                   + std::to_string(*idSource) + ", target: " + std::to_string(*idTarget));

        std::vector<T_ptr> params(2);
        std::unique_ptr<Result> retval = std::make_unique<Result>();
        retval->code = Error::CORE__ACCESS_DENIED;
        retval->success = false;
        params[0] = std::move(retval);
        params[1] = std::make_unique<Buffer>();

        sendMsg(client, res, params);
        return;
    }
}

std::shared_ptr<Promise> AuthRMC::getOrRegisterUserPassword(uint32_t pid) {
    std::shared_ptr<Promise> resultPromise = std::make_shared<Promise>();

    auto dbCmd = db::Database::craftGetGameServerAccessCommand(pid);
    db->runCommand(std::move(dbCmd), queueMutex, queueCV, promisesQueue)
        ->then([pid, resultPromise, this](std::any&& resultsAny) {

        std::unique_ptr<db::Result> result = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

        if (result->getStatus() != db::DBResultStatus::SUCCESS) {
            resultPromise->setResolveValue(std::move(std::optional<std::string>(std::nullopt)));
            resultPromise->resolve();
            return;
        }

        if (result->hasData()) {
            resultPromise->setResolveValue(std::move(std::optional(result->getData<db::DBGameServerAccessData>().password)));
            resultPromise->resolve();
        } else {
            logger->log(Logger::level::INFO, logGroup, "Registering new user password for PID " + std::to_string(pid));

            // Generate a new password for the user
            std::string newPassword = utils::generateUserPassword();
            auto insertCmd = db::Database::craftInsertGameServerAccessCommand(pid, newPassword);
            db->runCommand(std::move(insertCmd), queueMutex, queueCV, promisesQueue)
                ->then([resultPromise, newPassword](std::any&& resultsAny) {

                std::unique_ptr<db::Result> insertResult = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

                if (insertResult->getStatus() == db::DBResultStatus::SUCCESS) {
                    resultPromise->setResolveValue(std::move(std::optional(newPassword)));
                } else {
                    resultPromise->setResolveValue(std::move(std::optional<std::string>(std::nullopt)));
                }
                resultPromise->resolve();
            });
        }
    });

    return resultPromise;
}

} // namespace nex::rmc