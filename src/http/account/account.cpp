#include "account.hpp"
#include "../../crypto/tools.hpp"
#include "../../constants.hpp"
#include "../../grpc/channelPool.hpp"
#include "../../grpc/asyncRequest.hpp"

#include <unordered_map>
#include <auth.grpc.pb.h>
#include <nlohmann/json.hpp>
#include <date/tz.h>

// TODO Replace use of time_t with date EVERYWHERE. I write this here because it's the first time I use date in the project.

namespace acc {

using json = nlohmann::json;

std::shared_ptr<grpcimpl::ChannelPool> channelPool = nullptr;
std::map<std::string, std::vector<std::pair<std::string, std::string>>> gameServerHosts;
std::map<std::string, size_t> gameServerHostIndexRoundRobin;

json timezones;

/*
 * Handler for GET https://account.<domain>/v1/api/admin/time
 * Doesn't actually return anything, but the time is in the response headers.
 * Since it's such a simple request, we won't require a device certificate.
 */
void v1_api_admin_time(http::Server* srv, std::shared_ptr<http::Context> ctx) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
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
void v1_api_admin_mapped_ids(http::Server* srv, std::shared_ptr<http::Context> ctx,
                             const std::shared_ptr<db::Database>& db,
                             const std::shared_ptr<SettingsManager>& settingsManager,
                             const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("input_type") || !ctx->request->hasQuery("output_type") || !ctx->request->hasQuery("input")) {
        res = createError(ctx->request->getVersion(), 3, "Request parameters missing", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string inputType = ctx->request->getQuery("input_type");
    std::string outputType = ctx->request->getQuery("output_type");

    if ((inputType != "pid" && inputType != "user_id") || (outputType != "pid" && outputType != "user_id")) {
        res = createError(ctx->request->getVersion(), 1, "Unable to process request", "Bad Request", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::vector<std::string> input = util::split(ctx->request->getQuery("input"), ",");
    // We limit the input to 100 entries, as it would be too much to handle anything more than that
    if (input.empty() || input.size() > 100) {
        res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::vector<std::shared_ptr<Promise>> promises;
    for (const auto& id : input) {
        std::unique_ptr<db::Command> cmd;
        if (inputType == "pid") {
            if (id.empty()) continue;

            if (!std::ranges::all_of(id, [](char c) { return std::isdigit(c); })) {
                res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            int pid;
            try {
                pid = std::stoi(id);
            } catch ([[maybe_unused]] const std::out_of_range &e) {
                res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            cmd = db::Database::craftGetUserByPIDCommand(pid);
        } else {
            cmd = db::Database::craftGetUserByUsernameCommand(id);
        }

        promises.push_back(db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue));
    }

    std::make_shared<PromiseAll>(std::move(promises))
        ->setContext(std::move(std::pair(ctx->clientSockId, ctx->request)))
        .then([ctx = std::move(ctx),
               inputType = std::move(inputType),
               outputType = std::move(outputType),
               input = std::move(input),
               srv](std::any&& resultsAny) mutable {

        std::vector<std::unique_ptr<db::Result>> resultsList = std::move(std::any_cast<std::vector<std::unique_ptr<db::Result>>>(std::move(resultsAny)));

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
void v1_api_access_token_gen(http::Server* srv, std::shared_ptr<http::Context> ctx,
                             const std::shared_ptr<db::Database>& db,
                             const std::shared_ptr<SettingsManager>& settingsManager,
                             const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_POST) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
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
        res = createError(ctx->request->getVersion(), 1600, "Unable to process request", "Bad Request", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unordered_map<std::string, std::string> bodyMap;
    http::parseQuery(body, bodyMap);

    if (bodyMap.find("grant_type") == bodyMap.end() || (bodyMap["grant_type"] != "password" && bodyMap["grant_type"] != "refresh_token")) {
        res = createError(ctx->request->getVersion(), 4, "Invalid Grant Type", "grant_type", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (bodyMap["grant_type"] == "password") {
        if (bodyMap.find("user_id") == bodyMap.end() || bodyMap.find("password") == bodyMap.end()) {
            res = createError(ctx->request->getVersion(), 3, "Request parameters missing", "", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        std::string userId = bodyMap["user_id"];

        std::unique_ptr<db::Command> cmd = db::Database::craftGetUserByUsernameCommand(userId);
        db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
            ->setContext(std::move(std::pair<uint32_t, std::shared_ptr<http::Request>>(ctx->clientSockId, ctx->request)))
            .then([ctx = std::move(ctx), userId = std::move(userId),
                   deviceId, bodyMap = std::move(bodyMap),
                   srv, settingsManager](std::any&& resultsAny) mutable {

            std::unique_ptr<db::Result> results = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

            if (results->getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

            if (!results->hasData()) {
                std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 106, "Invalid account ID or password", "", HTTP_STATUS_FORBIDDEN);
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
                std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 106, "Invalid account ID or password", "", HTTP_STATUS_FORBIDDEN);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                return;
            }

            // At this point, the username is found and the password is correct
            crypto::AccountToken token {
                    .pid = userData.pid,
                    .deviceId = deviceId,
                    .expiration = static_cast<uint64_t>(time(nullptr)) + 3600,
                    .key = crypto::base64Decode(settingsManager->getTokenKey())
            };

            ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " logged in successfully.");

            std::string accountToken = crypto::generateAccountToken(token);

            token.key = crypto::base64Decode(settingsManager->getRefreshTokenKey());
            token.expiration = static_cast<uint64_t>(time(nullptr)) + 12 * 3600; // 12 hours for refresh token
            std::string refreshToken = crypto::generateAccountToken(token);

            pugi::xml_document doc;

            pugi::xml_node oauth20 = doc.append_child("OAuth20");
            pugi::xml_node access_token = oauth20.append_child("access_token");
            access_token.append_child("token").text().set(accountToken.c_str(), accountToken.length());
            access_token.append_child("refresh_token").text().set(refreshToken.c_str(), refreshToken.length());
            access_token.append_child("expires_in").text().set("3600");

            std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion(), doc);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        });
    } else {
        if (bodyMap.find("refresh_token") == bodyMap.end()) {
            res = createError(ctx->request->getVersion(), 3, "Missing refresh_token", "refresh_token", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        std::string refreshTokenStr = bodyMap["refresh_token"];
        crypto::AccountToken refreshToken {
            .key = crypto::base64Decode(settingsManager->getRefreshTokenKey())
        };

        if (!crypto::parseAccountToken(refreshTokenStr, refreshToken)) {
            res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        if (time(nullptr) > refreshToken.expiration) {
            res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        crypto::AccountToken token {
            .pid = refreshToken.pid,
            .deviceId = refreshToken.deviceId,
            .expiration = static_cast<uint64_t>(time(nullptr)) + 3600,
            .key = crypto::base64Decode(settingsManager->getTokenKey())
        };

        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                    "User with PID " + std::to_string(refreshToken.pid) + " refreshed their access token.");

        std::string accountToken = crypto::generateAccountToken(token);

        token.key = crypto::base64Decode(settingsManager->getRefreshTokenKey());
        token.expiration = static_cast<uint64_t>(time(nullptr)) + 12 * 3600; // 12 hours for refresh token
        std::string refresh = crypto::generateAccountToken(token);

        pugi::xml_document doc;

        pugi::xml_node oauth20 = doc.append_child("OAuth20");
        pugi::xml_node access_token = oauth20.append_child("access_token");
        access_token.append_child("token").text().set(accountToken.c_str(), accountToken.length());
        access_token.append_child("refresh_token").text().set(refresh.c_str(), refresh.length());
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
void v1_api_provider_nex_token(http::Server* srv, std::shared_ptr<http::Context> ctx,
                               const std::shared_ptr<db::Database>& db,
                               const std::shared_ptr<SettingsManager>& settingsManager,
                               const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("game_server_id")) {
        res = createError(ctx->request->getVersion(), 3, "Missing game_server_id", "game_server_id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string gameServerId = ctx->request->getQuery("game_server_id");

    if (gameServerId.size() != 8 || !std::ranges::all_of(gameServerId,
                                                         [](const char c) { return std::isxdigit(c); })) {
        res = createError(ctx->request->getVersion(), 1, "game_server_id is invalid", "game_server_id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    crypto::AccountToken accountToken;
    if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    const std::pair<std::string, std::string> host = gameServerHosts[gameServerId][gameServerHostIndexRoundRobin[gameServerId]];
    gameServerHostIndexRoundRobin[gameServerId] = (gameServerHostIndexRoundRobin[gameServerId] + 1) % gameServerHosts[gameServerId].size();

    auto channel = channelPool->getChannel(host.second);
    if (!channel) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                         "Failed to get channel for game server " + gameServerId);
        res = createError(ctx->request->getVersion(), 1018, "Failure to generate game server token", "", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    auto request = std::make_shared<grpcimpl::auth::v1::GetGameServerCredentialsRequest>();
    request->set_pid(accountToken.pid);
    request->set_gameserverid(gameServerId);

    // Create the stub and call the gRPC method
    auto stub = grpcimpl::auth::v1::AuthService::NewStub(channel);

    auto caller = std::shared_ptr<grpcimpl::AsyncRequest<grpcimpl::auth::v1::AuthService::Stub, grpcimpl::auth::v1::GetGameServerCredentialsRequest, grpcimpl::auth::v1::GetGameServerCredentialsResponse>>(
            new grpcimpl::AsyncRequest(
        std::move(stub), &grpcimpl::auth::v1::AuthService::Stub::async::GetGameServerCredentials,
        ctx->promisesQueue, ctx->queueMutex, ctx->queueCV));

    caller->call(std::move(request), settingsManager->getAccountsgRPCRequestTimeout())
        ->setContext(std::move(std::pair(ctx->clientSockId, ctx->request)))
        .then([ctx = std::move(ctx), gameServerId = std::move(gameServerId),
               gameServerHost = host.first, srv, settingsManager, accountToken = std::move(accountToken),
               stub = std::move(caller), clientContext = std::make_unique<grpc::ClientContext>()](std::any&& responseAny) mutable {

        auto response = std::make_shared<std::pair<std::shared_ptr<grpcimpl::auth::v1::GetGameServerCredentialsResponse>, grpc::Status>>(
            std::move(std::any_cast<std::pair<std::shared_ptr<grpcimpl::auth::v1::GetGameServerCredentialsResponse>, grpc::Status>>(std::move(responseAny))));

        if (!response->second.ok()) {
            ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                             "Failed to get game server credentials for game server " + gameServerId);
            // TODO Change the error to maintenance
            std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 1018,
                                                              "Failure to generate game server token", "", HTTP_STATUS_INTERNAL_SERVER_ERROR);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        if (!response->first->success()) {
            std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 1016, "NEX account not found", "", HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        json jwtPayload = {
                {"exp", time(nullptr) + 3600},
                {"iss", "account"},
                {"sub", accountToken.pid},
                {"game_server_id", gameServerId}
        };

        const std::string tokenJwt = crypto::signJWT(settingsManager->getNEXTokenKey(), jwtPayload);

        const auto nexPassword = response->first->password();

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
void v1_api_people_me_profile(http::Server* srv, std::shared_ptr<http::Context> ctx,
                              const std::shared_ptr<db::Database>& db,
                              const std::shared_ptr<SettingsManager>& settingsManager,
                              const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    crypto::AccountToken accountToken;
    if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftGetUserProfileCommand(accountToken.pid);
    db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
        ->setContext(std::move(std::pair(ctx->clientSockId, ctx->request)))
        .then([ctx = std::move(ctx), accountToken = std::move(accountToken),
               settingsManager, db, srv](std::any&& profileResultsAny) mutable {

        std::unique_ptr<db::Result> profileResults = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(profileResultsAny))));

        if (profileResults->getStatus() != db::DBResultStatus::SUCCESS) {
            throw std::runtime_error("Database error");
        }

        std::unique_ptr<db::Command> cmd = db::Database::craftGetDeviceAttributesCommand(accountToken.pid, accountToken.deviceId);
        db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
            ->setContext(std::move(std::pair(ctx->clientSockId, ctx->request)))
            .then([ctx = std::move(ctx), profileResults = std::move(profileResults),
                   accountToken = std::move(accountToken), settingsManager, db, srv](std::any&& deviceResultsAny) mutable {

            std::unique_ptr<db::Result> deviceResults = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(deviceResultsAny))));

            if (deviceResults->getStatus() != db::DBResultStatus::SUCCESS) {
                throw std::runtime_error("Database error");
            }

            if (!profileResults->hasData()) {
                std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not found", "", HTTP_STATUS_NOT_FOUND);
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

            // Calculate UTC offset in seconds from the timezone name

            // ATTENTION Code explorer!
            // You have stumbled upon a time and timezone related section of the code!
            // These sections are probably one of the worst a developer can deal with.
            // The code below uses the date library to get the current offset. It CAN'T be stored
            // as a static value, because the offset can change due to daylight saving time and the
            // real server takes this into account.

            int64_t utcOffset = 0;

            auto tz = date::locate_zone(userProfile.tz);
            if (!tz) {
                ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT,
                                 "Invalid timezone " + userProfile.tz + " for user with PID " + std::to_string(userProfile.pid));
            } else {
                auto now = std::chrono::system_clock::now();
                date::zoned_time zt{tz, now};
                auto info = zt.get_info();
                utcOffset = std::chrono::duration_cast<std::chrono::seconds>(info.offset).count();
            }

            person.append_child("utc_offset").text().set(std::to_string(utcOffset).c_str(), std::to_string(utcOffset).length());

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

            if (userProfile.emailValidated) {
                std::string emailValidatedDateStr = util::getDateISO8601(userProfile.emailValidatedDate);
                email.append_child("validated_date").text().set(emailValidatedDateStr.c_str(), emailValidatedDateStr.length());
            }

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

void v1_api_provider_service_token_me(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                      const std::shared_ptr<db::Database>& db,
                                      const std::shared_ptr<SettingsManager>& settingsManager,
                                      const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    crypto::AccountToken accountToken;
    if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("client_id") || !ctx->request->hasHeader("x-nintendo-title-id")) {
        res = createError(ctx->request->getVersion(), 1201, "The requested game server was not found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string clientId = ctx->request->getQuery("client_id");
    std::string titleId = ctx->request->getHeader("x-nintendo-title-id")[0];

    // Currently only returning maintenance
    res = createError(ctx->request->getVersion(), 2002, "The requested game server is under maintenance", "", HTTP_STATUS_BAD_REQUEST);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://mii-secure.account.<domain>/<type>.<format>?id=<mii id>
 * Obtains the image of the Mii with the given id.
 */
void mii_image(http::Server* srv, std::shared_ptr<http::Context> ctx,
               const std::shared_ptr<db::Database>& db,
               const std::shared_ptr<SettingsManager>& settingsManager,
               const std::shared_ptr<CertManager>& certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::string type =ctx->request->getPath();
    // Split the path into the type and format
    type = type.substr(1, type.find('.') - 1);
    std::string format = ctx->request->getPath().substr(type.length() + 2);

    fs::path miiImagesPath = settingsManager->getMiiImagesPath();
    if (!fs::exists(miiImagesPath)) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Mii images path does not exist.");
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!ctx->request->hasQuery("id")) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    fs::path miiImagePath = miiImagesPath / (ctx->request->getQuery("id") + "_" + type + "." + format);

    // For security purposes, we don't want to expose the file system structure. We make sure the resulting path is inside
    // the mii images directory.
    auto absoluteImagesPath = fs::absolute(miiImagesPath);
    auto absoluteImagePath = fs::absolute(miiImagePath);
    if (absoluteImagePath.string().rfind(absoluteImagesPath.string(), 0)) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!fs::exists(miiImagePath) || !fs::is_regular_file(miiImagePath)) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::ifstream file(miiImagePath, std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
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

/*
 * Handler for GET https://account.<domain>/v1/api/content/agreements/<type>/<country>/<version>
 * Obtains the EULA for the given type and country and version.
 */
void v1_api_content_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx,
                               const std::string& type, const std::string& country, const std::string& version,
                               const std::shared_ptr<db::Database>& db,
                               const std::shared_ptr<SettingsManager>& settingsManager,
                               const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    std::string language = "en";
    if (ctx->request->hasHeader("accept-language")) {
        language = ctx->request->getHeader("accept-language")[0];
    }

    std::optional<int> versionInt = std::nullopt;
    if (version != "@latest") {
        // Check if version is a valid integer
        try {
            versionInt = std::stoi(version);
        } catch (const std::invalid_argument&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid version", "version", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        } catch (const std::out_of_range&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid version", "version", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }
    }

    int blockLength = 2048;
    if (ctx->request->hasQuery("length")) {
        try {
            blockLength = std::stoi(ctx->request->getQuery("length"));
        } catch (const std::invalid_argument&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid length", "length", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        } catch (const std::out_of_range&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid length", "length", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftGetAgreementCommand(type, country, language, versionInt);
    db->runCommand(std::move(cmd), ctx->queueMutex, ctx->queueCV, ctx->promisesQueue)
        ->setContext(std::move(std::pair(ctx->clientSockId, ctx->request)))
        .then([ctx = std::move(ctx), srv, blockLength, type, country, versionInt, language, version](std::any&& resultsAny) mutable {

        std::unique_ptr<db::Result> results = std::make_unique<db::Result>(std::move(std::any_cast<db::Result>(std::move(resultsAny))));

        if (results->getStatus() != db::DBResultStatus::SUCCESS) {
            ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Database error while getting agreement");
            std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            return;
        }

        // Build the XML document
        pugi::xml_document doc;
        pugi::xml_node agreements = doc.append_child("agreements");
        pugi::xml_node agreement = agreements.append_child("agreement");
        agreement.append_child("country").text().set(country.c_str(), country.length());
        agreement.append_child("language").text().set(language.c_str(), language.length());
        agreement.append_child("type").text().set(type.c_str(), type.length());

        pugi::xml_node texts = agreement.append_child("texts");
        texts.append_attribute("xmlns:xsi") = "http://www.w3.org/2001/XMLSchema-instance";
        texts.append_attribute("xsi:type") = "chunkedStoredAgreementText";

        if (!results->hasData()) {
            // Create a dummy agreement if none was found
            ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT,
                             "No agreement found for type " + type + ", country " + country + ", language " + language + ", version " + version);

            std::string versionStr = "0001";
            if (versionInt.has_value()) {
                std::ostringstream oss;
                oss << std::setw(4) << std::setfill('0') << versionInt.value();
                versionStr = oss.str();
            }

            agreement.append_child("version").text().set(versionStr.c_str(), versionStr.length());
            std::string currentDate = util::getDateISO8601(std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));
            agreement.append_child("publish_date").text().set(currentDate.c_str(), currentDate.length());
            agreement.append_child("language_name").text().set("Unknown");

            texts.append_child("agree_text").append_child(pugi::node_cdata).set_value("Agree");
            texts.append_child("non_agree_text").append_child(pugi::node_cdata).set_value("Disagree");
            texts.append_child("main_title").append_child(pugi::node_cdata).set_value("SplatIt Contract");
            texts.append_child("sub_title").append_child(pugi::node_cdata).set_value("SplatIt Privacy Policy");

            std::string dummyText = "Hey, if you are reading this, it means the server administrator has not set up "
                                    "the agreements for your country and/or language. Please contact them to fix this.\r\n";

            // Split the text into chunks of blockLength bytes
            int chunkIdx = 1;
            for (size_t i = 0; i < dummyText.length(); i += blockLength) {
                std::string chunk = dummyText.substr(i, blockLength);
                pugi::xml_node mainChunk = texts.append_child("main_text");
                mainChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
                mainChunk.append_attribute("index") = chunkIdx;

                pugi::xml_node subTextChunk = texts.append_child("sub_text");
                subTextChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
                subTextChunk.append_attribute("index") = chunkIdx++;
            }
        } else {
            auto agreementData = std::move(results->getData<db::DBAgreementData>());
            std::ostringstream oss;
            oss << std::setw(4) << std::setfill('0') << agreementData.version;
            std::string versionStr = oss.str();

            agreement.append_child("version").text().set(versionStr.c_str(), versionStr.length());
            std::string publishDate = util::getDateISO8601(agreementData.publishedAt);
            agreement.append_child("publish_date").text().set(publishDate.c_str(), publishDate.length());
            agreement.append_child("language_name").text().set(agreementData.languageName.c_str(), agreementData.languageName.length());
            texts.append_child("agree_text").append_child(pugi::node_cdata).set_value(agreementData.agreeText.c_str(), agreementData.agreeText.length());
            texts.append_child("non_agree_text").append_child(pugi::node_cdata).set_value(agreementData.disagreeText.c_str(), agreementData.disagreeText.length());
            texts.append_child("main_title").append_child(pugi::node_cdata).set_value(agreementData.mainTitle.c_str(), agreementData.mainTitle.length());
            texts.append_child("sub_title").append_child(pugi::node_cdata).set_value(agreementData.subTitle.c_str(), agreementData.subTitle.length());

            size_t pos = 0;
            while ((pos = agreementData.mainText.find("\r\n", pos)) != std::string::npos) {
                agreementData.mainText.replace(pos, 2, "\n");
                pos += 1;
            }

            for (auto& c : agreementData.mainText) {
                if (c == '\r') c = '\n';
            }

            std::string mainTextResult;
            mainTextResult.reserve(agreementData.mainText.size());

            for (size_t i = 0; i < agreementData.mainText.size(); ++i) {
                if (agreementData.mainText[i] == '\n') {
                    if (i == 0 || agreementData.mainText[i - 1] != '\r') {
                        mainTextResult += '\r';
                    }
                }

                mainTextResult += agreementData.mainText[i];
            }

            pos = 0;
            while ((pos = agreementData.subText.find("\r\n", pos)) != std::string::npos) {
                agreementData.subText.replace(pos, 2, "\n");
                pos += 1;
            }

            for (auto& c : agreementData.subText) {
                if (c == '\r') c = '\n';
            }

            std::string subTextResult;
            subTextResult.reserve(agreementData.subText.size());
            for (size_t i = 0; i < agreementData.subText.size(); ++i) {
                if (agreementData.subText[i] == '\n') {
                    if (i == 0 || agreementData.subText[i - 1] != '\r') {
                        subTextResult += '\r';
                    }
                }

                subTextResult += agreementData.subText[i];
            }

            if (mainTextResult.at(mainTextResult.size() - 1) != '\n') {
                mainTextResult += "\r\n"; // Ensure the last line ends with a newline
            }

            if (subTextResult.at(subTextResult.size() - 1) != '\n') {
                subTextResult += "\r\n"; // Ensure the last line ends with a newline
            }

            int chunkIdx = 1;
            for (int i = 0; i < mainTextResult.size(); i += blockLength) {
                std::string chunk = mainTextResult.substr(i, blockLength);
                pugi::xml_node mainChunk = texts.append_child("main_text");
                mainChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
                mainChunk.append_attribute("index") = chunkIdx++;
            }

            chunkIdx = 1; // Reset chunk index for sub text
            for (int i = 0; i < subTextResult.size(); i += blockLength) {
                std::string chunk = subTextResult.substr(i, blockLength);
                pugi::xml_node subTextChunk = texts.append_child("sub_text");
                subTextChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
                subTextChunk.append_attribute("index") = chunkIdx++;
            }
        }

        std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion(), doc);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    });
}

/*
 * Handler for GET https://account.<domain>/v1/api/content/time_zones/<country>/<language>
 * Obtains the available time zones for the given country in the specified language.
 */
void v1_api_content_timezones(http::Server* srv, std::shared_ptr<http::Context> ctx,
                              const std::string& country, const std::string& language,
                              const std::shared_ptr<SettingsManager>& settingsManager,
                              const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    std::unique_ptr<http::Response> res;
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    if (!timezones.contains(country) || !timezones[country].contains(language)) {
        res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        return;
    }

    pugi::xml_document doc;
    pugi::xml_node timezonesNode = doc.append_child("timezones");
    int order = 1;
    for (const auto& timezone : timezones[country][language]) {
        std::string area = timezone["area"].get<std::string>();
        std::string name = timezone["name"].get<std::string>();

        // ATTENTION Code explorer!
        // You have stumbled upon a time and timezone related section of the code!
        // These sections are probably one of the worst a developer can deal with.
        // The code below uses the date library to get the current offset. It CAN'T be stored
        // as a static value, because the offset can change due to daylight saving time and the
        // real server takes this into account.

        auto tz = date::locate_zone(area);
        if (!tz) {
            ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT,
                             "Invalid timezone area: " + area + " for country: " + country + ", language: " + language);
            continue;
        }

        auto now = std::chrono::system_clock::now();
        date::zoned_time zt{tz, now};
        auto info = zt.get_info();

        int64_t utcOffset = std::chrono::duration_cast<std::chrono::seconds>(info.offset).count();

        pugi::xml_node timezoneNode = timezonesNode.append_child("timezone");

        timezoneNode.append_child("area").text().set(area.c_str(), area.length());
        timezoneNode.append_child("name").text().set(name.c_str(), name.length());
        timezoneNode.append_child("utc_offset").text().set(std::to_string(utcOffset).c_str(), std::to_string(utcOffset).length());
        timezoneNode.append_child("order").text().set(std::to_string(order).c_str(), std::to_string(order).length());
        timezoneNode.append_child("language").text().set(language.c_str(), language.length());

        order++;
    }

    res = prepareResponse(ctx->request->getVersion(), doc, HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

std::unique_ptr<http::Response> createError(http::Version version, int code, const std::string& message, const std::string& cause, const int httpStatus) {
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

    return prepareResponse(version, doc, httpStatus);
}

void errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx) {
    std::unique_ptr<http::Response> res;
    switch (ctx->status) {
        case HTTP_STATUS_NOT_FOUND:
            res = std::move(createError(ctx->request->getVersion(), 8, "Not Found", "", ctx->status));
            break;
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        default:
            res = std::move(createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error", ctx->status));
            break;
    }

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

std::unique_ptr<http::Response> prepareResponse(http::Version version, int httpStatus) {
    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(version, httpStatus);
    res->setHeader("X-Nintendo-Date", util::getXNintendoDateHeader());
    res->setHeader("Date", util::getDateHeader());
    res->setHeader("Server", "Nintendo 3DS (http)");
    if (version == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    return res;
}

std::unique_ptr<http::Response> prepareResponse(http::Version version, pugi::xml_document& doc, const int httpStatus) {
    // Set declaration
    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    decl.append_attribute("standalone") = "yes";

    std::stringstream ss;
    doc.save(ss);

    std::string body = ss.str();
    std::vector<uint8_t> bodyVec(body.begin(), body.end());

    std::unique_ptr<http::Response> res = prepareResponse(version, httpStatus);
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

    const std::vector<uint8_t> signature = std::vector(certBin.begin() + 0x4, certBin.begin() + 0x40);
    const std::vector<uint8_t> certData = std::vector(certBin.begin() + 0x80, certBin.end());

    return crypto::verifyECDSASignature(signature, certData, pubKey);
}

bool checkOauthToken(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                     crypto::AccountToken& token) {
    if (!req->hasHeader("authorization")) {
        return false;
    }

    std::string tokenStr = req->getHeader("authorization")[0];

    if (tokenStr.substr(0, 7) != "Bearer ") {
        return false;
    }

    tokenStr = tokenStr.substr(7);
    token.key = crypto::base64Decode(settingsManager->getTokenKey());

    if (!crypto::parseAccountToken(tokenStr, token) || time(nullptr) > token.expiration) {
        return false;
    }

    return true;
}

bool checkRequestParams(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<CertManager>& certManager, std::unique_ptr<http::Response>& resOut,
                        bool checkDevice) {
    if (checkDevice) {
        if (!req->hasHeader("x-nintendo-device-cert")) {
            resOut = createError(req->getVersion(), 110, "Unlinked device", "", HTTP_STATUS_FORBIDDEN);
            return false;
        }

        std::string deviceCert = req->getHeader("x-nintendo-device-cert")[0];
        EVP_PKEY* realWiiUKey = crypto::loadPublicKey(WII_U_PUB_KEY);
        EVP_PKEY* genWiiUKey = certManager->getDeviceKey();
        if (!(settingsManager->allowRealWiiU() && checkDeviceCert(deviceCert, realWiiUKey))
            && !(settingsManager->allowGeneratedWiiU() && checkDeviceCert(deviceCert, genWiiUKey))) {
            EVP_PKEY_free(realWiiUKey);
            resOut = createError(req->getVersion(), 1600, "Unable to process request", "Bad Request", HTTP_STATUS_BAD_REQUEST);
            return false;
        }

        EVP_PKEY_free(realWiiUKey);
    }

    // TODO Maybe check more headers?

    return true;
}

bool init(const std::shared_ptr<Logger::Logger>& logger) {
    fs::path timezonesFilePath = fs::path("timezones.json");
    if (!fs::exists(timezonesFilePath) || !fs::is_regular_file(timezonesFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The timezones file does not exist or is not a file, cannot initialize the account server.");
        return false;
    }

    try {
        std::ifstream timezonesFile(timezonesFilePath);
        timezones = json::parse(timezonesFile);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to parse timezones file: " + std::string(e.what()));
        return false;
    }

    return true;
}

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<CertManager> certMgr, std::shared_ptr<db::Database> db) {

    channelPool = std::make_shared<grpcimpl::ChannelPool>(settingsMgr->getAccountsgRPCConnectionPoolMaxSize());
    gameServerHosts = std::move(settingsMgr->getGameServerHosts());
    for (const auto& host : gameServerHosts) {
        gameServerHostIndexRoundRobin[host.first] = 0;
    }

    std::string domain = settingsMgr->getTopDomain();

    server->registerRoute("account." + domain, "/v1/api/admin/time", v1_api_admin_time);

    server->registerRoute("account." + domain, "/v1/api/admin/mapped_ids",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                v1_api_admin_mapped_ids(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/oauth20/access_token/generate",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                v1_api_access_token_gen(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/provider/nex_token/@me",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              v1_api_provider_nex_token(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/profile",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              v1_api_people_me_profile(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/provider/service_token/@me",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              v1_api_provider_service_token_me(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/content/agreements/([A-Za-z\-]+)/([A-Z]{2})/(\d{4}|@latest)$)",
                               [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   // Extract the type, country, and version from the request path
                                   const std::regex re(R"(^/v1/api/content/agreements/([A-Za-z\-]+)/([A-Z]{2})/(\d{4}|@latest)$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   std::string type = match[1];
                                   const std::string country = match[2];
                                   const std::string version = match[3];

                                   // Turn type uppercase
                                   std::ranges::transform(type, type.begin(), toupper);

                                   v1_api_content_agreements(srv, std::move(ctx), type, country, version, db, settingsMgr, certMgr);
                               });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/content/time_zones/([A-Z]{2})/([a-z]{2})$)",
                               [settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   const std::regex re(R"(^/v1/api/content/time_zones/([A-Z]{2})/([a-z]{2})$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   const std::string country = match[1];
                                   const std::string language = match[2];

                                   v1_api_content_timezones(srv, std::move(ctx), country, language, settingsMgr, certMgr);
                               });

    constexpr std::array<std::string_view, 7> miiTypes = {"normal_face", "frustrated_face", "happy_face", "like_face",
                                                          "puzzled_face", "surprised_face", "whole_body"};
    for (const auto& type : miiTypes) {
        server->registerRoute("mii-secure.account." + domain, "/" + std::string(type) + ".png",
                              [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                  mii_image(srv, std::move(ctx), db, settingsMgr, certMgr);
                              });
    }

    server->registerRoute("mii-secure.account." + domain, "/standard.tga",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              mii_image(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerErrorPage("account." + domain, errorHandler);
    server->registerErrorPage("mii-secure.account." + domain, errorHandler);
}

} // namespace acc