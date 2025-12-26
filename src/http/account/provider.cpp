#include "account.hpp"

#include "../../crypto/tools.hpp"
#include "../../grpc/asyncRequest.hpp"

namespace acc {

using namespace async;

/*
 * Handler for GET https://account.<domain>/v1/api/provider/nex_token/@me
 * Creates an access token for the given NEX game server.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
Task<void> v1_api_provider_nex_token(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                    std::shared_ptr<db::Database> db,
                                    std::shared_ptr<SettingsManager> settingsManager,
                                    std::shared_ptr<CertManager> certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!ctx->request->hasQuery("game_server_id")) {
        res = createError(ctx->request->getVersion(), 3, "Missing game_server_id", "game_server_id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string gameServerId = ctx->request->getQuery("game_server_id");

    if (gameServerId.size() != 8 || !std::ranges::all_of(gameServerId,
                                                         [](const char c) { return std::isxdigit(c); })) {
        res = createError(ctx->request->getVersion(), 1, "game_server_id is invalid", "game_server_id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    crypto::AccountToken accountToken;
    if (!co_await checkOauthToken(ctx->request, settingsManager, db, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!gameServerHosts.contains(gameServerId)) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                         "Game server ID " + gameServerId + " is not supported");
        res = createError(ctx->request->getVersion(), 118, "Game server ID not supported", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    const std::pair<std::string, std::string> host = gameServerHosts[gameServerId][gameServerHostIndexRoundRobin[gameServerId]];
    gameServerHostIndexRoundRobin[gameServerId] = (gameServerHostIndexRoundRobin[gameServerId] + 1) % gameServerHosts[gameServerId].size();

    auto channel = channelPool->getChannel(host.second);
    if (!channel) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                         "Failed to get channel for game server " + gameServerId);
        res = createError(ctx->request->getVersion(), 1018, "Failure to generate game server token", "", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::auth::v1::GetGameServerCredentialsRequest>();
    request->set_pid(accountToken.pid);
    request->set_gameserverid(gameServerId);

    // Create the stub and call the gRPC method
    auto stub = grpcimpl::auth::v1::AuthService::NewStub(channel);

    std::pair<std::shared_ptr<grpcimpl::auth::v1::GetGameServerCredentialsResponse>, grpc::Status> response =
        co_await grpcimpl::callAsync<
            grpcimpl::auth::v1::AuthService::Stub,
            void (grpcimpl::auth::v1::AuthService::Stub::async::*)(
                grpc::ClientContext*,
                const grpcimpl::auth::v1::GetGameServerCredentialsRequest*,
                grpcimpl::auth::v1::GetGameServerCredentialsResponse*,
                std::function<void(grpc::Status)>
            ),
            grpcimpl::auth::v1::GetGameServerCredentialsRequest,
            grpcimpl::auth::v1::GetGameServerCredentialsResponse
        >(
            stub,
            &grpcimpl::auth::v1::AuthService::Stub::async::GetGameServerCredentials,
            std::move(request),
            settingsManager->getAccountsgRPCRequestTimeout()
        );

    // TODO Try with other servers to see if we find one that works instead of just failing
    if (!response.second.ok()) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                         "Failed to get game server credentials for game server " + gameServerId);
        // TODO Change the error to maintenance
        res = createError(ctx->request->getVersion(), 1018,
                                                          "Failure to generate game server token", "", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!response.first->success()) {
        res = createError(ctx->request->getVersion(), 1016, "NEX account not found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto userInfoCmd = db::Database::craftGetUserByPIDCommand(accountToken.pid);
    auto userInfoResult = co_await db->runCommand(std::move(userInfoCmd));
    if (userInfoResult.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (!userInfoResult.hasData()) {
        res = createError(ctx->request->getVersion(), 130, "Account not found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json jwtPayload = {
            {"exp", time(nullptr) + 3600},
            {"iss", "account"},
            {"sub", accountToken.pid},
            {"username", userInfoResult.getData<db::DBUserData>().username},
            {"game_server_id", gameServerId}
    };

    const std::string tokenJwt = crypto::signJWT(settingsManager->getNEXTokenKey(), jwtPayload);

    const auto nexPassword = response.first->password();

    pugi::xml_document doc;
    pugi::xml_node nex_token = doc.append_child("nex_token");
    nex_token.append_child("pid").text().set(std::to_string(accountToken.pid).c_str(), std::to_string(accountToken.pid).length());
    nex_token.append_child("nex_password").text().set(nexPassword.c_str(), nexPassword.length());
    nex_token.append_child("token").text().set(tokenJwt.c_str(), tokenJwt.length());

    auto gameServerIp = host.first.substr(0, host.first.find(':'));
    auto gameServerPort = host.first.substr(host.first.find(':') + 1);

    nex_token.append_child("host").text().set(gameServerIp.c_str(), gameServerIp.length());
    nex_token.append_child("port").text().set(gameServerPort.c_str(), gameServerPort.length());

    ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                     "User with PID " + std::to_string(accountToken.pid) + " successfully obtained NEX token for game server " + gameServerId);

    res = prepareResponse(ctx->request->getVersion(), doc);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/provider/service_token/@me
 * Obtains the service token for the given client ID and title ID.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 *
 * Currently only returns a maintenance error.
 */
Task<void> v1_api_provider_service_token_me(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                            std::shared_ptr<db::Database> db,
                                            std::shared_ptr<SettingsManager> settingsManager,
                                            std::shared_ptr<CertManager> certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    crypto::AccountToken accountToken;
    if (!co_await checkOauthToken(ctx->request, settingsManager, db, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!ctx->request->hasQuery("client_id") || !ctx->request->hasHeader("x-nintendo-title-id")) {
        res = createError(ctx->request->getVersion(), 1201, "The requested game server was not found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string clientId = ctx->request->getQuery("client_id");
    std::string titleId = ctx->request->getHeader("x-nintendo-title-id")[0];

    // Currently only returning maintenance
    res = createError(ctx->request->getVersion(), 2002, "The requested game server is under maintenance", "", HTTP_STATUS_BAD_REQUEST);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

} // namespace acc