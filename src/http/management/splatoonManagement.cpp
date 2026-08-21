#include "management.hpp"

#include "../../util/util.hpp"
#include "../../grpc/asyncRequest.hpp"

#include <splatoon.grpc.pb.h>
#include <google/protobuf/empty.pb.h>

namespace mgm {

using json = nlohmann::json;

/*
 * Helper function to try a gRPC request with fallback to other splatoon servers
 */
template <typename Service, typename Method, typename Request, typename Response>
async::Task<std::pair<std::shared_ptr<Response>, grpc::Status>> callSplatoonServerWithFallback(
    const std::shared_ptr<http::Context>& ctx,
    Method method,
    std::shared_ptr<Request> request,
    int timeoutMs
) {
    auto it = serverHosts.find(ServerType::SPLATOON_SECURE);
    if (it == serverHosts.end() || it->second.empty()) {
        grpc::Status status(grpc::StatusCode::UNAVAILABLE, "No Splatoon servers configured");
        co_return std::make_pair(nullptr, status);
    }

    const auto& hostList = it->second;
    size_t startIndex = serverHostsIndexRoundRobin[ServerType::SPLATOON_SECURE];
    size_t currentIndex = startIndex;

    // Try all servers
    do {
        const auto& host = hostList[currentIndex];
        ctx->logger->log(Logger::level::DEBUG, Logger::group::MANAGEMENT,
                        "Trying Splatoon server at " + host);

        auto channel = channelPool->getChannel(host);
        if (channel) {
            auto stub = Service::NewStub(channel);

            std::pair<std::shared_ptr<Response>, grpc::Status> response =
                co_await grpcimpl::callAsync<
                    typename Service::Stub,
                    Method,
                    Request,
                    Response
                >(
                    stub,
                    method,
                    request,
                    timeoutMs
                );

            // As long as the server responds, consider it a success
            // (even if the gRPC status is not OK). Errors will be treated in the
            // caller.
            if (response.first != nullptr) {
                // Success! Update round-robin index
                serverHostsIndexRoundRobin[ServerType::SPLATOON_SECURE] = (currentIndex + 1) % hostList.size();
                co_return response;
            }

            ctx->logger->log(Logger::level::WARN, Logger::group::MANAGEMENT,
                            "Failed to contact Splatoon server at " + host +
                            ": " + response.second.error_message());
        } else {
            ctx->logger->log(Logger::level::WARN, Logger::group::MANAGEMENT,
                            "Failed to create channel to Splatoon server at " + host);
        }

        currentIndex = (currentIndex + 1) % hostList.size();
    } while (currentIndex != startIndex);

    // All servers failed
    grpc::Status status(grpc::StatusCode::UNAVAILABLE, "All Splatoon servers unreachable");
    co_return std::make_pair(nullptr, status);
}

/*
 * Handler for GET /api/v1/splatoon/client_count
 *
 * Returns the total number of registered clients across all splatoon servers.
 */
async::Task<void> mgm_get_splatoon_client_count(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<google::protobuf::Empty>();
    auto response = co_await callSplatoonServerWithFallback<
        grpcimpl::splatoon::v1::SplatoonService,
        void (grpcimpl::splatoon::v1::SplatoonService::Stub::async::*)(
            grpc::ClientContext*,
            const google::protobuf::Empty*,
            grpcimpl::splatoon::v1::GetConnectedClientCountResponse*,
            std::function<void(grpc::Status)>
        ),
        google::protobuf::Empty,
        grpcimpl::splatoon::v1::GetConnectedClientCountResponse
    >(
        ctx,
        &grpcimpl::splatoon::v1::SplatoonService::Stub::async::GetConnectedClientCount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to contact Splatoon servers: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Convert protobuf response to JSON
    json responseBody;
    responseBody["count"] = response.first->count();

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
    co_return;
}

/*
 * Handler for GET /api/v1/splatoon/lobby_count
 *
 * Returns the total number of lobbies across all splatoon servers.
 */
async::Task<void> mgm_get_splatoon_lobby_count(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<google::protobuf::Empty>();
    auto response = co_await callSplatoonServerWithFallback<
        grpcimpl::splatoon::v1::SplatoonService,
        void (grpcimpl::splatoon::v1::SplatoonService::Stub::async::*)(
            grpc::ClientContext*,
            const google::protobuf::Empty*,
            grpcimpl::splatoon::v1::GetLobbyCountResponse*,
            std::function<void(grpc::Status)>
        ),
        google::protobuf::Empty,
        grpcimpl::splatoon::v1::GetLobbyCountResponse
    >(
        ctx,
        &grpcimpl::splatoon::v1::SplatoonService::Stub::async::GetLobbyCount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to contact Splatoon servers: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Convert protobuf response to JSON
    json responseBody;
    responseBody["count"] = response.first->count();

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
    co_return;
}

/*
 * Handler for GET /api/v1/splatoon/lobbies
 *
 * Returns a list of all lobbies across all splatoon servers, including their current player count and other info.
 */
async::Task<void> mgm_get_splatoon_lobbies(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<google::protobuf::Empty>();
    auto response = co_await callSplatoonServerWithFallback<
        grpcimpl::splatoon::v1::SplatoonService,
        void (grpcimpl::splatoon::v1::SplatoonService::Stub::async::*)(
            grpc::ClientContext*,
            const google::protobuf::Empty*,
            grpcimpl::splatoon::v1::GetExistingLobbiesResponse*,
            std::function<void(grpc::Status)>
        ),
        google::protobuf::Empty,
        grpcimpl::splatoon::v1::GetExistingLobbiesResponse
    >(
        ctx,
        &grpcimpl::splatoon::v1::SplatoonService::Stub::async::GetExistingLobbies,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to contact Splatoon servers: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Convert protobuf response to JSON
    json responseBody;
    responseBody["lobbies"] = json::array();

    for (const auto& lobby : response.first->lobbies()) {
        json lobbyJson;
        lobbyJson["gId"] = lobby.gid();
        lobbyJson["ownerPid"] = lobby.ownerpid();
        lobbyJson["hostPid"] = lobby.hostpid();
        lobbyJson["minParticipants"] = lobby.minparticipants();
        lobbyJson["maxParticipants"] = lobby.maxparticipants();
        lobbyJson["participationPolicy"] = lobby.participationpolicy();
        lobbyJson["policyArgument"] = lobby.policyargument();
        lobbyJson["flags"] = lobby.flags();
        lobbyJson["state"] = lobby.state();
        lobbyJson["description"] = lobby.description();
        lobbyJson["gameMode"] = lobby.gamemode();

        lobbyJson["attributes"] = json::array();
        for (const auto& attr : lobby.attributes()) lobbyJson["attributes"].push_back(attr);

        lobbyJson["openParticipation"] = lobby.openparticipation();
        lobbyJson["matchmakeSystemType"] = lobby.matchmakesystemtype();
        lobbyJson["option0"] = lobby.option0();
        lobbyJson["userPasswordEnabled"] = lobby.userpasswordenabled();
        lobbyJson["systemPasswordEnabled"] = lobby.systempasswordenabled();
        lobbyJson["startedTime"] = lobby.startedtime().seconds();

        lobbyJson["playerPids"] = json::array();
        for (const auto& pid : lobby.playerpids()) lobbyJson["playerPids"].push_back(pid);

        responseBody["lobbies"].push_back(lobbyJson);
    }

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
    co_return;
}

/*
 * Handler for GET /api/v1/splatoon/festival_totals?festivalId=X
 *
 * Returns the total wins and user counts per team for a festival.
 */
async::Task<void> mgm_get_festival_totals(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    if (!ctx->request->hasQuery("festivalId")) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "Missing required query parameter: festivalId",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    uint32_t festivalId;
    try {
        festivalId = std::stoul(ctx->request->getQuery("festivalId"));
    } catch (...) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "Invalid festivalId parameter",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::splatoon::v1::GetFestivalTotalsRequest>();
    request->set_festivalid(festivalId);

    auto response = co_await callSplatoonServerWithFallback<
        grpcimpl::splatoon::v1::SplatoonService,
        void (grpcimpl::splatoon::v1::SplatoonService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::splatoon::v1::GetFestivalTotalsRequest*,
            grpcimpl::splatoon::v1::GetFestivalTotalsResponse*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::splatoon::v1::GetFestivalTotalsRequest,
        grpcimpl::splatoon::v1::GetFestivalTotalsResponse
    >(
        ctx,
        &grpcimpl::splatoon::v1::SplatoonService::Stub::async::GetFestivalTotals,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to contact Splatoon servers: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["totals"] = json::array();
    for (const auto& total : response.first->totals()) {
        json totalJson;
        totalJson["team"] = total.team();
        totalJson["userCount"] = total.usercount();
        totalJson["totalWins"] = total.totalwins();
        responseBody["totals"].push_back(totalJson);
    }

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
    co_return;
}

} // namespace mgm