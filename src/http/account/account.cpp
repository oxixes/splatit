#include "account.hpp"
#include "../../crypto/tools.hpp"
#include "../../constants.hpp"

#include <unordered_map>

namespace acc {

/*
 * Handler for GET https://account.<domain>/v1/api/admin/time
 * Doesn't actually return anything, but the time is in the response headers.
 * Since it's such a simple request, we won't require a device certificate.
 */
void v1_api_admin_time(http::Server* srv, std::unique_ptr<http::Context> ctx) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion());
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/admin/mapped_ids
 * Returns a list of mapped ids for the given input.
 * The input can be either a principal id or a username.
 * Requires a device certificate.
 */
void v1_api_admin_mapped_ids(http::Server* srv, std::unique_ptr<http::Context> ctx,
                             const std::shared_ptr<db::Database>& db,
                             const std::shared_ptr<SettingsManager>& settingsManager,
                             const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("input_type") || !ctx->request->hasQuery("output_type") || !ctx->request->hasQuery("input")) {
        res = createError(ctx->request->getVersion(), 3, "Request parameters missing", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string inputType = ctx->request->getQuery("input_type");
    std::string outputType = ctx->request->getQuery("output_type");

    if ((inputType != "pid" && inputType != "user_id") || (outputType != "pid" && outputType != "user_id")) {
        res = createError(ctx->request->getVersion(), 1, "Unable to process request", "Bad Request");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::vector<std::string> input = util::split(ctx->request->getQuery("input"), ",");
    // We limit the input to 100 entries, as it would be too much to handle anything more than that
    if (input.empty() || input.size() > 100) {
        res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::vector<std::shared_ptr<Promise<std::unique_ptr<db::Result>>>> promises;
    for (const auto& id : input) {
        std::unique_ptr<db::Command> cmd;
        if (inputType == "pid") {
            if (!std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit(c); })) {
                res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input");
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            int pid;
            try {
                pid = std::stoi(id);
            } catch (const std::out_of_range &e) {
                res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input");
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            cmd = db::Database::craftGetUserByPIDCommand(pid);
        } else {
            cmd = db::Database::craftGetUserByUsernameCommand(id);
        }

        promises.push_back(db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue));
    }

    std::make_shared<PromiseAll<std::unique_ptr<db::Result>>>(std::move(promises))
        ->setContext(std::move(std::pair<uint32_t, std::shared_ptr<http::Request>>(ctx->clientSockId, ctx->request)))
        .then([ctx = std::move(ctx),
               inputType = std::move(inputType),
               outputType = std::move(outputType),
               input = std::move(input),
               srv](std::vector<std::unique_ptr<db::Result>> resultsList) mutable {

        pugi::xml_document doc;
        pugi::xml_node mapped_ids = doc.append_child("mapped_ids");

        size_t i = 0;

        for (auto& results : resultsList) {
            if (results->getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

            auto userData = (results->hasData()) ? std::move(results->getData<db::DBUserData>()) : db::DBUserData{};
            auto userPid = (results->hasData()) ? std::to_string(userData.pid) : ((inputType == "pid") ? input.at(i) : "");
            auto username = (results->hasData()) ? userData.username : ((inputType == "user_id") ? input.at(i) : "");

            pugi::xml_node mapped_id = mapped_ids.append_child("mapped_id");
            if (inputType == "pid")
                mapped_id.append_child("in_id").text().set(userPid.c_str(), userPid.size());
            else
                mapped_id.append_child("in_id").text().set(username.c_str(), username.size());

            if (outputType == "pid")
                mapped_id.append_child("out_id").text().set(userPid.c_str(), userPid.size());
            else
                mapped_id.append_child("out_id").text().set(username.c_str(), username.size());

            i++;
        }

        std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion(), doc);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    });
}

/*
 * Handler for POST https://account.<domain>/v1/api/oauth20/access_token/generate
 * Generates an access token for the given user.
 * Requires a device certificate. The password can be given directly or as a hash.
 */
void v1_api_access_token_gen(http::Server* srv, std::unique_ptr<http::Context> ctx,
                             const std::shared_ptr<db::Database>& db,
                             const std::shared_ptr<SettingsManager>& settingsManager,
                             const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_POST) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string deviceCert = ctx->request->getHeader("x-nintendo-device-cert")[0];
    std::vector<uint8_t> certBin = crypto::base64Decode(deviceCert);
    std::string deviceIdHex(certBin.begin() + 0xC6, certBin.begin() + 0xCE);
    // We convert the hex string to a number
    uint32_t deviceId = std::stoul(deviceIdHex, nullptr, 16);

    if (!ctx->request->hasHeader("content-type") || ctx->request->getHeader("content-type")[0] != "application/x-www-form-urlencoded") {
        res = createError(ctx->request->getVersion(), 1600, "Unable to process request", "Bad Request");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unordered_map<std::string, std::string> bodyMap;
    http::parseQuery(body, bodyMap);

    if (bodyMap.find("grant_type") == bodyMap.end() || (bodyMap["grant_type"] != "password" && bodyMap["grant_type"] != "refresh_token")) {
        res = createError(ctx->request->getVersion(), 4, "Invalid Grant Type", "grant_type");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (bodyMap["grant_type"] == "password") {
        if (bodyMap.find("user_id") == bodyMap.end() || bodyMap.find("password") == bodyMap.end()) {
            res = createError(ctx->request->getVersion(), 3, "Request parameters missing", "");
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        std::string userId = bodyMap["user_id"];

        std::unique_ptr<db::Command> cmd = db::Database::craftGetUserByUsernameCommand(userId);
        db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
            ->setContext(std::move(std::pair<uint32_t, std::shared_ptr<http::Request>>(ctx->clientSockId, ctx->request)))
            .then([ctx = std::move(ctx), userId = std::move(userId),
                   deviceId, bodyMap = std::move(bodyMap),
                   srv, db, settingsManager](std::unique_ptr<db::Result> results) mutable {

            if (results->getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

            if (!results->hasData()) {
                std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 106, "Invalid account ID or password", "");
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            auto userData = std::move(results->getData<db::DBUserData>());

            std::string nintendoPasswordHash;
            if (bodyMap.find("password_type") != bodyMap.end() && bodyMap["password_type"] == "hash") {
                nintendoPasswordHash = bodyMap["password"];
            } else {
                nintendoPasswordHash = crypto::genNintendoPasswordHash(userData.pid, bodyMap["password"]);
            }

            // Verifying a password takes a while, which could allow an attacker to distinguish between
            // valid and invalid usernames. To prevent this, we could always verify a password, even if the
            // username is invalid. But since the mapped_ids method already allows to find valid usernames,
            // we can just return an error if the username is invalid without care for timing attacks.

            // Another possible attack is a DDoS by sending a lot of requests with incorrect passwords.
            // To prevent this, we could rate limit requests, but we'll leave it as is for now.
            if (!crypto::verifyPassword(nintendoPasswordHash, userData.password)) {
                ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " tried to log in with "
                                                                                                 "invalid password (client " + util::ipv4ToString(ctx->client) + ").");
                std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 106, "Invalid account ID or password", "");
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            // At this point, the username is found and the password is correct
            crypto::AccountToken token {
                    .pid = userData.pid,
                    .deviceId = deviceId,
                    .expiration = (uint64_t) time(nullptr) + 3600,
                    .key = crypto::base64Decode(settingsManager->getTokenKey())
            };

            ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " logged in successfully.");

            std::string tokenJwt = crypto::generateAccountToken(token);

            token.key = crypto::base64Decode(settingsManager->getRefreshTokenKey());
            std::string refreshJwt = crypto::generateAccountToken(token);

            pugi::xml_document doc;

            pugi::xml_node oauth20 = doc.append_child("OAuth20");
            pugi::xml_node access_token = oauth20.append_child("access_token");
            access_token.append_child("token").text().set(tokenJwt.c_str(), tokenJwt.length());
            access_token.append_child("refresh_token").text().set(refreshJwt.c_str(), refreshJwt.length());
            access_token.append_child("expires_in").text().set("3600");

            std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion(), doc);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        });
    } else {
        if (bodyMap.find("refresh_token") == bodyMap.end()) {
            res = createError(ctx->request->getVersion(), 3, "Missing refresh_token", "refresh_token");
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        std::string refreshTokenStr = bodyMap["refresh_token"];
        crypto::AccountToken refreshToken {
            .key = crypto::base64Decode(settingsManager->getRefreshTokenKey())
        };

        if (!crypto::parseAccountToken(refreshTokenStr, refreshToken)) {
            res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        if (time(nullptr) > refreshToken.expiration) {
            res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        crypto::AccountToken token {
            .pid = refreshToken.pid,
            .deviceId = refreshToken.deviceId,
            .expiration = (uint64_t) time(nullptr) + 3600,
            .key = crypto::base64Decode(settingsManager->getTokenKey())
        };

        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                    "User with PID " + std::to_string(refreshToken.pid) + " refreshed their access token.");

        std::string tokenJwt = crypto::generateAccountToken(token);

        token.key = crypto::base64Decode(settingsManager->getRefreshTokenKey());
        std::string refreshJwt = crypto::generateAccountToken(token);

        pugi::xml_document doc;

        pugi::xml_node oauth20 = doc.append_child("OAuth20");
        pugi::xml_node access_token = oauth20.append_child("access_token");
        access_token.append_child("token").text().set(tokenJwt.c_str(), tokenJwt.length());
        access_token.append_child("refresh_token").text().set(refreshJwt.c_str(), refreshJwt.length());
        access_token.append_child("expires_in").text().set("3600");

        res = prepareResponse(ctx->request->getVersion(), doc);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }
}

/*
 * Handler for GET https://account.<domain>/v1/api/provider/nex_token/@me
 * Creates an access token for the given NEX game server.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
void v1_api_provider_nex_token(http::Server* srv, std::unique_ptr<http::Context> ctx,
                               const std::shared_ptr<db::Database>& db,
                               const std::shared_ptr<SettingsManager>& settingsManager,
                               const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("game_server_id")) {
        res = createError(ctx->request->getVersion(), 3, "Missing game_server_id", "game_server_id");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string gameServerId = ctx->request->getQuery("game_server_id");

    if (gameServerId.size() != 8 || !std::all_of(gameServerId.begin(),gameServerId.end(),
                                                                   [](char c) { return std::isxdigit(c); })) {
        res = createError(ctx->request->getVersion(), 1, "game_server_id is invalid", "game_server_id");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasHeader("authorization")) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string token = ctx->request->getHeader("authorization")[0];

    if (token.substr(0, 7) != "Bearer ") {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    token = token.substr(7);
    crypto::AccountToken accountToken {
        .key = crypto::base64Decode(settingsManager->getTokenKey())
    };
    if (!crypto::parseAccountToken(token, accountToken) || time(nullptr) > accountToken.expiration) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftGetGameServerAccessCommand(accountToken.pid, ctx->request->getQuery("game_server_id"));
    db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
        ->setContext(std::move(std::pair<uint32_t, std::shared_ptr<http::Request>>(ctx->clientSockId, ctx->request)))
        .then([ctx = std::move(ctx), gameServerId = std::move(gameServerId),
               accountToken = std::move(accountToken),
               db, settingsManager, srv](std::unique_ptr<db::Result> results) mutable {
        if (results->getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");
        if (!results->hasData()) {
            std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 1016, "NEX account not found", "");
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        json jwtPayload = {
                {"exp", time(nullptr) + 3600},
                {"iss", "account"},
                {"sub", accountToken.pid},
                {"game_server_id", gameServerId}
        };

        std::string tokenJwt = crypto::signJWT(settingsManager->getNEXTokenKey(), jwtPayload);

        auto gameServerAccess = std::move(results->getData<db::DBGameServerAccessData>());
        auto nexPassword = gameServerAccess.password;
        auto gameServerHost = settingsManager->getGameServerHost(gameServerId);

        pugi::xml_document doc;
        pugi::xml_node nex_token = doc.append_child("nex_token");
        nex_token.append_child("pid").text().set(std::to_string(accountToken.pid).c_str(), std::to_string(accountToken.pid).length());
        nex_token.append_child("nex_password").text().set(nexPassword.c_str(), nexPassword.length());
        nex_token.append_child("token").text().set(tokenJwt.c_str(), tokenJwt.length());

        auto gameServerIp = gameServerHost.substr(0, gameServerHost.find(':'));
        auto gameServerPort = gameServerHost.substr(gameServerHost.find(':') + 1);

        nex_token.append_child("host").text().set(gameServerIp.c_str(), gameServerIp.length());
        nex_token.append_child("port").text().set(gameServerPort.c_str(), gameServerPort.length());

        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                    "User with PID " + std::to_string(accountToken.pid) + " successfully obtained NEX token for game server " + gameServerId);

        std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion(), doc);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    });
}

/*
 * Handler for GET https://account.<domain>/v1/api/people/@me/profile
 * Obtains the profile of the user with the given principal id.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
void v1_api_people_me_profile(http::Server* srv, std::unique_ptr<http::Context> ctx,
                              const std::shared_ptr<db::Database>& db,
                              const std::shared_ptr<SettingsManager>& settingsManager,
                              const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasHeader("authorization")) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string token = ctx->request->getHeader("authorization")[0];

    if (token.substr(0, 7) != "Bearer ") {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    token = token.substr(7);
    crypto::AccountToken accountToken {
            .key = crypto::base64Decode(settingsManager->getTokenKey())
    };
    if (!crypto::parseAccountToken(token, accountToken) || time(nullptr) > accountToken.expiration) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftGetUserProfileCommand(accountToken.pid);
    db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
        ->setContext(std::move(std::pair<uint32_t, std::shared_ptr<http::Request>>(ctx->clientSockId, ctx->request)))
        .then([ctx = std::move(ctx), accountToken = std::move(accountToken),
               settingsManager, db, srv](std::unique_ptr<db::Result> profileResults) mutable {
        if (profileResults->getStatus() != db::DBResultStatus::SUCCESS) {
            throw std::runtime_error("Database error");
        }

        std::unique_ptr<db::Command> cmd = db::Database::craftGetDeviceAttributesCommand(accountToken.pid, accountToken.deviceId);
        db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
            ->setContext(std::move(std::pair<uint32_t, std::shared_ptr<http::Request>>(ctx->clientSockId, ctx->request)))
            .then([ctx = std::move(ctx), profileResults = std::move(profileResults),
                   accountToken = std::move(accountToken), settingsManager, db, srv](std::unique_ptr<db::Result> deviceResults) mutable {

            if (deviceResults->getStatus() != db::DBResultStatus::SUCCESS) {
                throw std::runtime_error("Database error");
            }

            if (!profileResults->hasData()) {
                // TODO Change this to appropiate error
                std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 1016, "NEX account not found", "");
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            auto userProfile = std::move(profileResults->getData<db::DBUserProfileData>());
            auto deviceAttributes = std::move(deviceResults->getData<std::vector<db::DBDeviceAttributeData>>());

            pugi::xml_document doc;
            pugi::xml_node person = doc.append_child("person");
            person.append_child("active_flag").text().set((userProfile.active) ? "Y" : "N", 1);
            person.append_child("birth_date").text().set(userProfile.birthdate.c_str(), userProfile.birthdate.length());
            person.append_child("country").text().set(userProfile.country.c_str(), userProfile.country.length());

            std::string createDate = util::getDateISO8601(userProfile.created);
            person.append_child("create_date").text().set(createDate.c_str(), createDate.length());

            person.append_child("gender").text().set((userProfile.gender) ? "F" : "M", 1);
            person.append_child("language").text().set(userProfile.language.c_str(), userProfile.language.length());

            std::string updatedDate = util::getDateISO8601(userProfile.updated);
            person.append_child("updated").text().set(updatedDate.c_str(), updatedDate.length());

            person.append_child("marketing_flag").text().set((userProfile.marketing) ? "Y" : "N", 1);
            person.append_child("off_device_flag").text().set((userProfile.offDevice) ? "Y" : "N", 1);

            std::string pidStr = std::to_string(userProfile.pid);
            person.append_child("pid").text().set(pidStr.c_str(), pidStr.length());

            std::string regionStr = std::to_string(userProfile.region);
            person.append_child("region").text().set(regionStr.c_str(), regionStr.length());

            person.append_child("tz_name").text().set(userProfile.tz.c_str(), userProfile.tz.length());
            person.append_child("user_id").text().set(userProfile.username.c_str(), userProfile.username.length());

            std::string utcOffsetStr = std::to_string(userProfile.utcOffset);
            person.append_child("utc_offset").text().set(utcOffsetStr.c_str(), utcOffsetStr.length());

            pugi::xml_node deviceAttributesNode = person.append_child("device_attributes");
            for (const auto& attr : deviceAttributes) {
                pugi::xml_node deviceAttribute = deviceAttributesNode.append_child("device_attribute");
                std::string createdDateStr = util::getDateISO8601(attr.createdDate);
                deviceAttribute.append_child("created_date").text().set(createdDateStr.c_str(), createdDateStr.length());
                deviceAttribute.append_child("name").text().set(attr.name.c_str(), attr.name.length());
                deviceAttribute.append_child("value").text().set(attr.value.c_str(), attr.value.length());
            }

            pugi::xml_node email = person.append_child("email");
            email.append_child("address").text().set(userProfile.email.c_str(), userProfile.email.length());

            std::string emailIdStr = std::to_string(userProfile.emailId);
            email.append_child("id").text().set(emailIdStr.c_str(), emailIdStr.length());

            email.append_child("parent").text().set((userProfile.emailParent) ? "Y" : "N", 1);
            email.append_child("primary").text().set((userProfile.emailPrimary) ? "Y" : "N", 1);
            email.append_child("reachable").text().set((userProfile.emailReachable) ? "Y" : "N", 1);
            email.append_child("type").text().set(userProfile.emailType.c_str(), userProfile.emailType.length());
            email.append_child("updated_by").text().set(userProfile.emailUpdatedBy.c_str(), userProfile.emailUpdatedBy.length());
            email.append_child("validated").text().set((userProfile.emailValidated) ? "Y" : "N", 1);

            std::string emailValidatedDateStr = util::getDateISO8601(userProfile.emailValidatedDate);
            email.append_child("validated_date").text().set(emailValidatedDateStr.c_str(), emailValidatedDateStr.length());

            pugi::xml_node mii = person.append_child("mii");
            mii.append_child("data").text().set(userProfile.miiData.c_str(), userProfile.miiData.length());

            std::string miiStatus = "COMPLETED";
            mii.append_child("status").text().set(miiStatus.c_str(), miiStatus.length());

            std::string miiIdStr = std::to_string(userProfile.miiId);
            mii.append_child("id").text().set(miiIdStr.c_str(), miiIdStr.length());

            mii.append_child("name").text().set(userProfile.miiName.c_str(), userProfile.miiName.length());
            mii.append_child("mii_hash").text().set(userProfile.miiHash.c_str(), userProfile.miiHash.length());
            mii.append_child("primary").text().set((userProfile.miiPrimary) ? "Y" : "N", 1);

            pugi::xml_node miiImages = mii.append_child("mii_images");
            pugi::xml_node miiImage = miiImages.append_child("mii_image");

            std::string miiType = "standard"; // Profile images are always standard
            miiImage.append_child("type").text().set(miiType.c_str(), miiType.length());

            std::string miiImageId = std::to_string(userProfile.miiId); // This should be its own id, but we'll use the mii id for now
            miiImage.append_child("id").text().set(miiImageId.c_str(), miiImageId.length());

            std::string miiImageUrl = "https://mii-secure.account." + settingsManager->getTopDomain() + "/standard.tga" +
                                      "?id=" + std::to_string(userProfile.miiId);

            miiImage.append_child("url").text().set(miiImageUrl.c_str(), miiImageUrl.length());
            miiImage.append_child("cached_url").text().set(miiImageUrl.c_str(), miiImageUrl.length());

            std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion(), doc);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        });
    });
}

void v1_api_provider_service_token_me(http::Server* srv, std::unique_ptr<http::Context> ctx,
                                      const std::shared_ptr<db::Database>& db,
                                      const std::shared_ptr<SettingsManager>& settingsManager,
                                      const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasHeader("authorization")) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string token = ctx->request->getHeader("authorization")[0];

    if (token.substr(0, 7) != "Bearer ") {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    token = token.substr(7);
    crypto::AccountToken accountToken {
            .key = crypto::base64Decode(settingsManager->getTokenKey())
    };
    if (!crypto::parseAccountToken(token, accountToken) || time(nullptr) > accountToken.expiration) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("client_id") || !ctx->request->hasHeader("x-nintendo-title-id")) {
        res = createError(ctx->request->getVersion(), 1201, "The requested game server was not found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string clientId = ctx->request->getQuery("client_id");
    std::string titleId = ctx->request->getHeader("x-nintendo-title-id")[0];

    // Currently only returning maintenance
    res = createError(ctx->request->getVersion(), 2002, "The requested game server is under maintenance", "");
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://mii-secure.account.<domain>/<type>.<format>?id=<mii id>
 * Obtains the image of the Mii with the given id.
 */
void mii_image(http::Server* srv, std::unique_ptr<http::Context> ctx,
               const std::shared_ptr<db::Database>& db,
               const std::shared_ptr<SettingsManager>& settingsManager,
               const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::string type =ctx->request->getPath();
    // Split the path into the type and format
    type = type.substr(1, type.find('.') - 1);
    std::string format = ctx->request->getPath().substr(type.length() + 2);

    fs::path miiImagesPath = settingsManager->getMiiImagesPath();
    if (!fs::exists(miiImagesPath)) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Mii images path does not exist.");
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("id")) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    fs::path miiImagePath = miiImagesPath / (ctx->request->getQuery("id") + "_" + type + "." + format);

    // For security purposes, we don't want to expose the file system structure. We make sure the resulting path is inside
    // the mii images directory.
    auto absoluteImagesPath = fs::absolute(miiImagesPath);
    auto absoluteImagePath = fs::absolute(miiImagePath);
    if (absoluteImagePath.string().rfind(absoluteImagesPath.string(), 0)) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!fs::exists(miiImagePath) || !fs::is_regular_file(miiImagePath)) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "");
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::ifstream file(miiImagePath, std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    res->setHeader("Content-Type", "application/octet-stream");
    res->setHeader("Date", util::getDateHeader());
    // Get last modified time
    std::filesystem::file_time_type lastModified = fs::last_write_time(miiImagePath);
    time_t lastModifiedTime = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<
            std::chrono::system_clock::duration>(lastModified - std::filesystem::file_time_type::clock::now() +
                                                 std::chrono::system_clock::now()));

    res->setHeader("Last-Modified", util::getDateHeader(lastModifiedTime));
    res->setHeader("Connection", "close");

    res->setBody(image);

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

std::unique_ptr<http::Response> createError(http::Version version, int code, const std::string& message, const std::string& cause) {
    pugi::xml_document doc;

    pugi::xml_node errors = doc.append_child("errors");
    pugi::xml_node error = errors.append_child("error");

    std::string codeStr;
    // Pad with 0s
    for (int i = 0; i < 4 - std::to_string(code).length(); i++) {
        codeStr += "0";
    }
    codeStr += std::to_string(code);

    error.append_child("cause").text().set(cause.c_str(), cause.size());
    error.append_child("code").text().set(codeStr.c_str(), codeStr.size());
    error.append_child("message").text().set(message.c_str(), message.size());

    return prepareResponse(version, doc);
}

void errorHandler(http::Server* srv, std::unique_ptr<http::Context> ctx) {
    std::unique_ptr<http::Response> res;
    switch (ctx->status) {
        case HTTP_STATUS_NOT_FOUND:
            res = std::move(createError(ctx->request->getVersion(), 8, "Not Found", ""));
            break;
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        default:
            res = std::move(createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error"));
            break;
    }

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

std::unique_ptr<http::Response> prepareResponse(http::Version version) {
    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(version, HTTP_STATUS_OK);
    res->setHeader("X-Nintendo-Date", util::getXNintendoDateHeader());
    res->setHeader("Date", util::getDateHeader());
    res->setHeader("Server", "Nintendo 3DS (http)");
    if (version == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    return res;
}

std::unique_ptr<http::Response> prepareResponse(http::Version version, pugi::xml_document& doc) {
    // Set declaration
    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    decl.append_attribute("standalone") = "yes";

    std::stringstream ss;
    doc.save(ss);

    std::string body = ss.str();
    std::vector<uint8_t> bodyVec(body.begin(), body.end());

    std::unique_ptr<http::Response> res = prepareResponse(version);
    res->setHeader("Content-Type", "application/xml;charset=UTF-8");
    res->setBody(std::move(bodyVec));

    return res;
}

bool checkDeviceCert(const std::string& cert, EVP_PKEY* pubKey) {
    std::vector<uint8_t> certBin;

    try {
        certBin = crypto::base64Decode(cert);
    } catch (std::runtime_error& e) {
        return false;
    }

    if (certBin.size() != 384) return false;

    std::vector<uint8_t> signature = std::vector<uint8_t>(certBin.begin() + 0x4, certBin.begin() + 0x40);
    std::vector<uint8_t> certData = std::vector<uint8_t>(certBin.begin() + 0x80, certBin.end());

    return crypto::verifyECDSASignature(signature, certData, pubKey);
}

bool checkRequestParams(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<CertManager>& certManager, std::unique_ptr<http::Response>& resOut,
                        bool checkDevice) {
    if (checkDevice) {
        if (!req->hasHeader("x-nintendo-device-cert")) {
            resOut = createError(req->getVersion(), 110, "Unlinked device", "");
            return false;
        }

        std::string deviceCert = req->getHeader("x-nintendo-device-cert")[0];
        EVP_PKEY* realWiiUKey = crypto::loadPublicKey(WII_U_PUB_KEY);
        EVP_PKEY* genWiiUKey = certManager->getDeviceKey();
        if (!(settingsManager->allowRealWiiU() && checkDeviceCert(deviceCert, realWiiUKey))
            && !(settingsManager->allowGeneratedWiiU() && checkDeviceCert(deviceCert, genWiiUKey))) {
            EVP_PKEY_free(realWiiUKey);
            resOut = createError(req->getVersion(), 1600, "Unable to process request", "Bad Request");
            return false;
        }

        EVP_PKEY_free(realWiiUKey);
    }

    // TODO Maybe check more headers?

    return true;
}

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<CertManager> certMgr, std::shared_ptr<db::Database> db) {

    std::string domain = settingsMgr->getTopDomain();

    server->registerRoute("account." + domain, "/v1/api/admin/time", v1_api_admin_time);

    server->registerRoute("account." + domain, "/v1/api/admin/mapped_ids",
                          [db, settingsMgr, certMgr](http::Server* srv, std::unique_ptr<http::Context> ctx) {
                                v1_api_admin_mapped_ids(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/oauth20/access_token/generate",
                          [db, settingsMgr, certMgr](http::Server* srv, std::unique_ptr<http::Context> ctx) {
                                v1_api_access_token_gen(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/provider/nex_token/@me",
                          [db, settingsMgr, certMgr](http::Server* srv, std::unique_ptr<http::Context> ctx) {
                              v1_api_provider_nex_token(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/profile",
                          [db, settingsMgr, certMgr](http::Server* srv, std::unique_ptr<http::Context> ctx) {
                              v1_api_people_me_profile(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/provider/service_token/@me",
                          [db, settingsMgr, certMgr](http::Server* srv, std::unique_ptr<http::Context> ctx) {
                              v1_api_provider_service_token_me(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    constexpr std::array<std::string_view, 7> miiTypes = {"normal_face", "frustrated_face", "happy_face", "like_face",
                                                          "puzzled_face", "surprised_face", "whole_body"};
    for (const auto& type : miiTypes) {
        server->registerRoute("mii-secure.account." + domain, "/" + std::string(type) + ".png",
                              [db, settingsMgr, certMgr](http::Server* srv, std::unique_ptr<http::Context> ctx) {
                                  mii_image(srv, std::move(ctx), db, settingsMgr, certMgr);
                              });
    }

    server->registerRoute("mii-secure.account." + domain, "/standard.tga",
                          [db, settingsMgr, certMgr](http::Server* srv, std::unique_ptr<http::Context> ctx) {
                              mii_image(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerErrorPage("account." + domain, errorHandler);
    server->registerErrorPage("mii-secure.account." + domain, errorHandler);
}

} // namespace acc