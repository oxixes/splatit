#include "management.hpp"

#include "../../util/util.hpp"
#include "../../constants.hpp"
#include "../../grpc/services/serverStatusService.hpp"
#include "../../grpc/asyncRequest.hpp"

namespace mgm {

std::shared_ptr<grpcimpl::ChannelPool> channelPool;
std::map<ServerType, std::vector<sock::IPv4Addr>> serverHosts;
std::map<ServerType, size_t> serverHostsIndexRoundRobin;

/*
 * Handler for GET /api/v1/status
 *
 * Returns the status of the management server.
 */
async::Task<void> mgm_status(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
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

    json responseBody = {
        {"status", "ok"},
        {"version", MANAGEMENT_SERVER_VERSION}
    };

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
    co_return;
}

/*
 * Handler for GET /api/v1/server-staus
 *
 * Pings all servers and returns their status.
 */
async::Task<void> mgm_server_status(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
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

    json responseBody;
    responseBody["servers"] = json::array();

    for (const auto& [type, hostList] : serverHosts) {
        for (const auto& host : hostList) {
            json serverStatus;
            std::string typeStr;
            switch (type) {
                case ServerType::ACCOUNT:
                    typeStr = "account";
                    break;
                case ServerType::BOSS:
                    typeStr = "boss";
                    break;
                case ServerType::FRIENDS_AUTH:
                    typeStr = "friends_auth";
                    break;
                case ServerType::SPLATOON_AUTH:
                    typeStr = "splatoon_auth";
                    break;
                case ServerType::FRIENDS_SECURE:
                    typeStr = "friends_secure";
                    break;
                case ServerType::SPLATOON_SECURE:
                    typeStr = "splatoon_secure";
                    break;
            }

            serverStatus["type"] = typeStr;
            serverStatus["address"] = util::ipv4WPortToString(host);

            ctx->logger->log(Logger::level::DEBUG, Logger::group::MANAGEMENT,
                             "Pinging " + typeStr + " server at " + util::ipv4WPortToString(host));

            // Ping the server
            auto channel = channelPool->getChannel(util::ipv4WPortToString(host));
            if (!channel) {
                // Return error response
                bool keepAlive = false;
                std::unique_ptr<http::Response> res = createError(ctx, ManagementError::INTERNAL_ERROR,
                    "Failed to create gRPC channel to " + util::ipv4ToString(host),
                    settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }

            auto request = std::make_shared<grpcimpl::serverstatus::v1::GetServerStatusRequest>();
            request->set_servertype(static_cast<grpcimpl::serverstatus::v1::ServerType>(type));

            // Create the stub and call the gRPC method
            auto stub = grpcimpl::serverstatus::v1::ServerStatusService::NewStub(channel);

            std::pair<std::shared_ptr<grpcimpl::serverstatus::v1::GetServerStatusResponse>, grpc::Status> response =
                co_await grpcimpl::callAsync<
                    grpcimpl::serverstatus::v1::ServerStatusService::Stub,
                    void (grpcimpl::serverstatus::v1::ServerStatusService::Stub::async::*)(
                        grpc::ClientContext*,
                        const grpcimpl::serverstatus::v1::GetServerStatusRequest*,
                        grpcimpl::serverstatus::v1::GetServerStatusResponse*,
                        std::function<void(grpc::Status)>
                    ),
                    grpcimpl::serverstatus::v1::GetServerStatusRequest,
                    grpcimpl::serverstatus::v1::GetServerStatusResponse
                >(
                    stub,
                    &grpcimpl::serverstatus::v1::ServerStatusService::Stub::async::GetServerStatus,
                    std::move(request),
                    settingsMgr->getManagementgRPCRequestTimeout()
                );

            if (!response.second.ok()) {
                serverStatus["status"] = "offline";
                serverStatus["message"] = "Failed to contact server: " + response.second.error_message();
            } else {
                serverStatus["status"] = response.first->isonline() ? "online" : "offline";
                if (!response.first->isonline()) {
                    serverStatus["message"] = response.first->message();
                }
            }

            responseBody["servers"].push_back(serverStatus);
        }
    }

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
    co_return;
}

std::unique_ptr<http::Response> createError(const std::shared_ptr<http::Context>& ctx, ManagementError code, const std::string& message, const std::string& corsOrigin, bool& keepAlive, int httpStatus) {
    json errorBody;
    errorBody["error"]["code"] = code;
    errorBody["error"]["message"] = message;

    std::unique_ptr<http::Response> res = prepareResponse(ctx, errorBody, keepAlive, corsOrigin, httpStatus);
    return res;
}

async::Task<void> errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    bool keepAlive = false;
    std::unique_ptr<http::Response> res;
    switch (ctx->status) {
        case HTTP_STATUS_NOT_FOUND:
            res = std::move(createError(ctx, ManagementError::NOT_FOUND, "Not Found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, ctx->status));
            break;
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        default:
            res = std::move(createError(ctx, ManagementError::INTERNAL_ERROR, "Unable to process request", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, ctx->status));
            break;
    }

    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
    co_return;
}

std::unique_ptr<http::Response> prepareResponse(const std::shared_ptr<http::Context>& ctx, bool& keepAlive, std::string corsOrigin, int httpStatus) {
    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), httpStatus);
    res->setHeader("Date", util::getDateHeader());
    res->setHeader("Server", "SplatIt");
    res->setHeader("Access-Control-Allow-Origin", corsOrigin);
    if (ctx->request->getVersion() == http::Version::HTTP_1_1) {
        if (ctx->request->hasHeader("connection") &&
            ctx->request->getHeader("connection")[0] == "keep-alive") {
            keepAlive = true;
            res->setHeader("Connection", "keep-alive");
        } else {
            keepAlive = false;
            res->setHeader("Connection", "close");
        }
    }

    return res;
}

std::unique_ptr<http::Response> prepareResponse(const std::shared_ptr<http::Context>& ctx, const json& body, bool& keepAlive, const std::string &corsOrigin, const int httpStatus) {
    std::unique_ptr<http::Response> res = prepareResponse(ctx, keepAlive, corsOrigin, httpStatus);
    std::string bodyStr = body.dump(-1, ' ', false, json::error_handler_t::replace);
    res->setBody(std::vector<uint8_t>(bodyStr.begin(), bodyStr.end()));
    res->setHeader("Content-Type", "application/json; charset=utf-8");

    return res;
}

std::unique_ptr<http::Response> prepareCORSPreflightResponse(const std::shared_ptr<http::Context>& ctx, const std::shared_ptr<SettingsManager>& settingsMgr, const std::string& allowedMethods, bool& keepAlive) {
    std::unique_ptr<http::Response> res = prepareResponse(ctx, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_NO_CONTENT);
    res->setHeader("Access-Control-Allow-Methods", allowedMethods);
    res->setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res->setHeader("Access-Control-Max-Age", "86400"); // 24 hours

    return res;
}

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<db::Database> db) {
    channelPool = std::make_shared<grpcimpl::ChannelPool>(settingsMgr->getManagementgRPCConnectionPoolMaxSize());
    serverHosts = std::move(settingsMgr->getManagementServerAddresses());
    for (const auto& [type, hostList] : serverHosts) {
        serverHostsIndexRoundRobin[type] = 0;
    }

    server->registerRoute("*", "/api/v1/status",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return mgm_status(srv, std::move(ctx), settingsMgr);
                         });

    server->registerRoute("*", "/api/v1/server-status",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return mgm_server_status(srv, std::move(ctx), settingsMgr);
                         });

    server->registerRoute("*", "/api/v1/agreements",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             // Route to appropriate handler based on method
                             auto method = ctx->request->getMethod();
                             if (method == http::Method::M_POST) {
                                 return mgm_publish_agreement(srv, std::move(ctx), settingsMgr);
                             }

                             if (method == http::Method::M_DELETE) {
                                 return mgm_delete_agreement(srv, std::move(ctx), settingsMgr);
                             }

                             return mgm_get_agreements(srv, std::move(ctx), settingsMgr);
                         });

    server->registerErrorPage("*",
                             [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                 return errorHandler(srv, std::move(ctx), settingsMgr);
                             });
}

} // namespace mgm