#include "management.hpp"

#include "../../util/util.hpp"
#include "../../grpc/asyncRequest.hpp"

#include <friends.grpc.pb.h>
#include <google/protobuf/empty.pb.h>

namespace mgm {

using json = nlohmann::json;

/*
 * Helper function to try connecting to friends servers in round-robin fashion
 * Returns a pair of channel and the address used (empty if all failed)
 */
std::pair<std::shared_ptr<grpc::Channel>, sock::IPv4Addr> getSplatoonServerChannel() {
    auto it = serverHosts.find(ServerType::FRIENDS_SECURE);
    if (it == serverHosts.end() || it->second.empty()) {
        return {nullptr, sock::IPv4Addr{}};
    }

    const auto& hostList = it->second;
    size_t startIndex = serverHostsIndexRoundRobin[ServerType::FRIENDS_SECURE];
    size_t currentIndex = startIndex;

    // Try all servers in round-robin fashion
    do {
        const auto& host = hostList[currentIndex];
        auto channel = channelPool->getChannel(util::ipv4WPortToString(host));

        if (channel) {
            // Update the round-robin index for next time
            serverHostsIndexRoundRobin[ServerType::FRIENDS_SECURE] = (currentIndex + 1) % hostList.size();
            return {channel, host};
        }

        currentIndex = (currentIndex + 1) % hostList.size();
    } while (currentIndex != startIndex);

    return {nullptr, sock::IPv4Addr{}};
}

/*
 * Helper function to try a gRPC request with fallback to other friends servers
 */
template <typename Service, typename Method, typename Request, typename Response>
async::Task<std::pair<std::shared_ptr<Response>, grpc::Status>> callSplatoonServerWithFallback(
    const std::shared_ptr<http::Context>& ctx,
    Method method,
    std::shared_ptr<Request> request,
    int timeoutMs
) {
    auto it = serverHosts.find(ServerType::FRIENDS_SECURE);
    if (it == serverHosts.end() || it->second.empty()) {
        grpc::Status status(grpc::StatusCode::UNAVAILABLE, "No friends servers configured");
        co_return std::make_pair(nullptr, status);
    }

    const auto& hostList = it->second;
    size_t startIndex = serverHostsIndexRoundRobin[ServerType::FRIENDS_SECURE];
    size_t currentIndex = startIndex;

    // Try all servers
    do {
        const auto& host = hostList[currentIndex];
        ctx->logger->log(Logger::level::DEBUG, Logger::group::MANAGEMENT,
                        "Trying friends server at " + util::ipv4WPortToString(host));

        auto channel = channelPool->getChannel(util::ipv4WPortToString(host));
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
                serverHostsIndexRoundRobin[ServerType::FRIENDS_SECURE] = (currentIndex + 1) % hostList.size();
                co_return response;
            }

            ctx->logger->log(Logger::level::WARN, Logger::group::MANAGEMENT,
                            "Failed to contact friends server at " + util::ipv4WPortToString(host) +
                            ": " + response.second.error_message());
        } else {
            ctx->logger->log(Logger::level::WARN, Logger::group::MANAGEMENT,
                            "Failed to create channel to friends server at " + util::ipv4WPortToString(host));
        }

        currentIndex = (currentIndex + 1) % hostList.size();
    } while (currentIndex != startIndex);

    // All servers failed
    grpc::Status status(grpc::StatusCode::UNAVAILABLE, "All friends servers unreachable");
    co_return std::make_pair(nullptr, status);
}

/*
 * Handler for GET /api/v1/friends/client_count
 *
 * Returns the total number of registered clients across all friends servers.
 */
async::Task<void> mgm_get_friends_client_count(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
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
        grpcimpl::friends::v1::FriendsService,
        void (grpcimpl::friends::v1::FriendsService::Stub::async::*)(
            grpc::ClientContext*,
            const google::protobuf::Empty*,
            grpcimpl::friends::v1::GetConnectedClientCountResponse*,
            std::function<void(grpc::Status)>
        ),
        google::protobuf::Empty,
        grpcimpl::friends::v1::GetConnectedClientCountResponse
    >(
        ctx,
        &grpcimpl::friends::v1::FriendsService::Stub::async::GetConnectedClientCount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to contact friends servers: " + response.second.error_message(),
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

} // namespace mgm