#include "management.hpp"
#include "bossManagement.hpp"

#include "../../util/util.hpp"
#include "../../constants.hpp"
#include "../../crypto/tools.hpp"
#include "../../grpc/services/serverStatusService.hpp"
#include "../../grpc/asyncRequest.hpp"

#include <accountManagement.grpc.pb.h>

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

async::Task<void> mgm_login(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_POST && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    json requestBody;
    try {
        const auto& body = ctx->request->getBody();
        requestBody = json::parse(std::string(body.begin(), body.end()));
    } catch (const std::exception&) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid JSON body",
                               settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    if (!requestBody.contains("username") || !requestBody["username"].is_string() ||
        !requestBody.contains("password") || !requestBody["password"].is_string()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Missing username or password",
                               settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    if (!serverHosts.contains(ServerType::ACCOUNT) || serverHosts[ServerType::ACCOUNT].empty()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "No account server configured",
                               settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_GATEWAY);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    const auto& hosts = serverHosts[ServerType::ACCOUNT];
    size_t& nextHost = serverHostsIndexRoundRobin[ServerType::ACCOUNT];
    const auto host = hosts[nextHost % hosts.size()];
    nextHost = (nextHost + 1) % hosts.size();

    auto channel = channelPool->getChannel(util::ipv4WPortToString(host));
    if (!channel) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to create gRPC channel to account server",
                               settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_GATEWAY);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AuthenticateManagementUserRequest>();
    request->set_username(requestBody["username"].get<std::string>());
    request->set_password(requestBody["password"].get<std::string>());

    auto stub = grpcimpl::accountmanagement::v1::AccountManagementService::NewStub(channel);
    std::pair<std::shared_ptr<grpcimpl::accountmanagement::v1::AuthenticateManagementUserResponse>, grpc::Status> response =
        co_await grpcimpl::callAsync<
            grpcimpl::accountmanagement::v1::AccountManagementService::Stub,
            void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
                grpc::ClientContext*,
                const grpcimpl::accountmanagement::v1::AuthenticateManagementUserRequest*,
                grpcimpl::accountmanagement::v1::AuthenticateManagementUserResponse*,
                std::function<void(grpc::Status)>
            ),
            grpcimpl::accountmanagement::v1::AuthenticateManagementUserRequest,
            grpcimpl::accountmanagement::v1::AuthenticateManagementUserResponse
        >(
            stub,
            &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::AuthenticateManagementUser,
            std::move(request),
            settingsMgr->getManagementgRPCRequestTimeout()
        );

    if (!response.second.ok()) {
        bool keepAlive = false;
        const int status = response.second.error_code() == grpc::StatusCode::PERMISSION_DENIED ?
            HTTP_STATUS_UNAUTHORIZED : HTTP_STATUS_BAD_GATEWAY;
        auto res = createError(ctx, status == HTTP_STATUS_UNAUTHORIZED ? ManagementError::PERMISSION_DENIED : ManagementError::BAD_GATEWAY,
                               status == HTTP_STATUS_UNAUTHORIZED ? "Invalid username or password" : response.second.error_message(),
                               settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, status);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    const time_t now = time(nullptr);
    const time_t expiration = now + MANAGEMENT_SESSION_DURATION_SECONDS;
    json payload = {
        {"iss", "management"},
        {"sub", response.first->pid()},
        {"username", response.first->username()},
        {"is_admin", response.first->isadmin()},
        {"iat", now},
        {"exp", expiration}
    };

    const std::string token = crypto::signJWT(settingsMgr->getTokenKey(), payload);
    json responseBody = {
        {"token", token},
        {"pid", response.first->pid()},
        {"username", response.first->username()},
        {"isAdmin", response.first->isadmin()},
        {"expiresAt", expiration}
    };

    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
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
        case HTTP_STATUS_BAD_REQUEST:
            res = std::move(createError(ctx, ManagementError::BAD_REQUEST, "Bad Request", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, ctx->status));
            break;
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

namespace {
std::vector<std::string> splitPath(const std::string& path) {
    auto parts = util::split(path, "/");
    std::vector<std::string> out;
    out.reserve(parts.size());
    for (const auto& p : parts) {
        if (!p.empty()) out.push_back(p);
    }
    return out;
}

std::optional<uint32_t> parseU32(const std::string& s) {
    try {
        const unsigned long v = std::stoul(s);
        if (v > std::numeric_limits<uint32_t>::max()) return std::nullopt;
        return static_cast<uint32_t>(v);
    } catch (...) {
        return std::nullopt;
    }
}

bool hasValidManagementToken(const std::shared_ptr<http::Context>& ctx, const std::shared_ptr<SettingsManager>& settingsMgr) {
    if (!ctx->request->hasHeader("authorization")) {
        return false;
    }

    const std::string authHeader = ctx->request->getHeader("authorization")[0];
    if (authHeader.size() <= 7 || authHeader.substr(0, 7) != "Bearer ") {
        return false;
    }

    const std::string token = authHeader.substr(7);
    if (!crypto::verifyJWT(settingsMgr->getTokenKey(), token)) {
        return false;
    }

    try {
        const size_t firstDot = token.find('.');
        const size_t lastDot = token.find_last_of('.');
        if (firstDot == std::string::npos || lastDot == std::string::npos || firstDot == lastDot) {
            return false;
        }

        const std::string payloadStr = token.substr(firstDot + 1, lastDot - firstDot - 1);
        const auto payloadVec = crypto::base64UrlDecode(payloadStr);
        const json payload = json::parse(std::string(payloadVec.begin(), payloadVec.end()));

        return payload.contains("iss") && payload["iss"].get<std::string>() == "management" &&
               payload.contains("exp") && time(nullptr) <= payload["exp"].get<time_t>() &&
               payload.contains("is_admin") && payload["is_admin"].get<bool>();
    } catch (const std::exception&) {
        return false;
    }
}

using ManagementHandler = std::function<async::Task<void>(http::Server*, std::shared_ptr<http::Context>)>;

async::Task<void> requireAdmin(http::Server* srv, std::shared_ptr<http::Context> ctx,
                               std::shared_ptr<SettingsManager> settingsMgr, ManagementHandler handler) {
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        co_await handler(srv, std::move(ctx));
        co_return;
    }

    if (!hasValidManagementToken(ctx, settingsMgr)) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::PERMISSION_DENIED, "Missing or invalid authorization token",
                               settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_UNAUTHORIZED);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    co_await handler(srv, std::move(ctx));
}
} // namespace

void registerRoutes(const std::shared_ptr<http::Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr,
                    const std::shared_ptr<db::Database>& mgmDb, const std::shared_ptr<Logger::Logger>& logger,
                    std::shared_ptr<grpc::ChannelCredentials> grpcCredentials) {
    channelPool = std::make_shared<grpcimpl::ChannelPool>(
        settingsMgr->getManagementgRPCConnectionPoolMaxSize(), grpcCredentials);
    serverHosts = std::move(settingsMgr->getManagementServerAddresses());
    for (const auto& [type, hostList] : serverHosts) {
        serverHostsIndexRoundRobin[type] = 0;
    }

    server->registerRoute("*", "/api/v1/status",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return mgm_status(srv, std::move(ctx), settingsMgr);
                         });

    server->registerRoute("*", "/api/v1/auth/login",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return mgm_login(srv, std::move(ctx), settingsMgr);
                         });

    server->registerRoute("*", "/api/v1/server-status",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_server_status(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    server->registerRoute("*", "/api/v1/agreements",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_POST) {
                                         return mgm_publish_agreement(srv, std::move(ctx), settingsMgr);
                                     }

                                     if (method == http::Method::M_DELETE) {
                                         return mgm_delete_agreement(srv, std::move(ctx), settingsMgr);
                                     }

                                     return mgm_get_agreements(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    // Devices collection
    server->registerRoute("*", "/api/v1/devices",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_POST) return mgm_create_device(srv, std::move(ctx), settingsMgr);
                                     return mgm_list_devices(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    // Accounts collection
    server->registerRoute("*", "/api/v1/accounts",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_POST) return mgm_create_account(srv, std::move(ctx), settingsMgr);
                                     return mgm_list_accounts(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    // Lookup by username
    server->registerRoute("*", "/api/v1/accounts:by-username",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_get_account_by_username(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    // Security status
    server->registerRoute("*", "/api/v1/security-status",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_PUT) {
                                         return mgm_update_security_status(srv, std::move(ctx), settingsMgr);
                                     }
                                     return mgm_get_security_status(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    // Generic prefix routing for subresources
    // NOTE: Any route that includes params/wildcards must use registerRegexRoute.
    server->registerRegexRoute("*", R"(^/api/v1/devices/([0-9]+)$)",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) -> async::Task<void> {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) -> async::Task<void> {
                                     const auto parts = splitPath(ctx->request->getPath());
                                     // expected: api v1 devices {id}
                                     if (parts.size() < 4) {
                                         return errorHandler(srv, std::move(ctx), settingsMgr);
                                     }
                                     auto id = parseU32(parts[3]);
                                     if (!id) {
                                         ctx->status =  HTTP_STATUS_BAD_REQUEST;
                                         return errorHandler(srv, std::move(ctx), settingsMgr);
                                     }

                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_GET) {
                                         return mgm_get_device(srv, std::move(ctx), settingsMgr, *id);
                                     }
                                     if (method == http::Method::M_DELETE) {
                                         return mgm_delete_device(srv, std::move(ctx), settingsMgr, *id);
                                     }
                                     // PATCH/PUT for update
                                     return mgm_update_device(srv, std::move(ctx), settingsMgr, *id);
                                 });
                         });

    server->registerRegexRoute("*", R"(^/api/v1/accounts/([0-9]+)(/.*)?$)",
                         [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) -> async::Task<void> {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) -> async::Task<void> {
                             const auto parts = splitPath(ctx->request->getPath());
                             // api v1 accounts {pid} ...
                             if (parts.size() < 4) {
                                 ctx->status = HTTP_STATUS_NOT_FOUND;
                                 return errorHandler(srv, std::move(ctx), settingsMgr);
                             }

                             auto pid = parseU32(parts[3]);
                             if (!pid) {
                                 ctx->status =  HTTP_STATUS_BAD_REQUEST;
                                 return errorHandler(srv, std::move(ctx), settingsMgr);
                             }

                             // /api/v1/accounts/{pid}
                             if (parts.size() == 4) {
                                 auto method = ctx->request->getMethod();
                                 if (method == http::Method::M_GET) {
                                     return mgm_get_account(srv, std::move(ctx), settingsMgr, *pid);
                                 }
                                 if (method == http::Method::M_DELETE) {
                                     return mgm_delete_account(srv, std::move(ctx), settingsMgr, *pid);
                                 }
                                 return mgm_update_account(srv, std::move(ctx), settingsMgr, *pid);
                             }

                             // subresources
                             const std::string& sub = parts[4];
                             if (sub == "email") {
                                 return mgm_update_account_email(srv, std::move(ctx), settingsMgr, *pid);
                             }

                             if (sub == "mii") {
                                 return mgm_set_account_mii(srv, std::move(ctx), settingsMgr, *pid);
                             }

                             if (sub == "cemu-files") {
                                 return mgm_get_cemu_files(srv, std::move(ctx), settingsMgr, *pid);
                             }

                             if (sub == "agreements") {
                                 auto method = ctx->request->getMethod();
                                 if (method == http::Method::M_POST) {
                                     return mgm_add_account_agreement(srv, std::move(ctx), settingsMgr, *pid);
                                 }
                                 return mgm_remove_account_agreement(srv, std::move(ctx), settingsMgr, *pid);
                             }

                             if (sub == "devices") {
                                 // /api/v1/accounts/{pid}/devices
                                 if (parts.size() == 5) {
                                     return mgm_link_device_to_account(srv, std::move(ctx), settingsMgr, *pid);
                                 }
                                 // /api/v1/accounts/{pid}/devices/{deviceId}[/attributes[/name]]
                                 auto deviceId = parseU32(parts[5]);
                                 if (!deviceId) {
                                     ctx->status = HTTP_STATUS_BAD_REQUEST;
                                     return errorHandler(srv, std::move(ctx), settingsMgr);
                                 }

                                 if (parts.size() == 6) {
                                     // unlink
                                     return mgm_unlink_device_from_account(srv, std::move(ctx), settingsMgr, *pid, *deviceId);
                                 }

                                 const std::string& sub2 = parts[6];
                                 if (sub2 == "status") {
                                     return mgm_update_account_device_status(srv, std::move(ctx), settingsMgr, *pid, *deviceId);
                                 }

                                 if (sub2 == "attributes") {
                                     if (parts.size() == 7) {
                                         return mgm_list_account_device_attributes(srv, std::move(ctx), settingsMgr, *pid, *deviceId);
                                     }
                                     const std::string& attrName = parts[7];
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_PUT) {
                                         return mgm_set_account_device_attribute(srv, std::move(ctx), settingsMgr, *pid, *deviceId, attrName);
                                     }
                                     return mgm_remove_account_device_attribute(srv, std::move(ctx), settingsMgr, *pid, *deviceId, attrName);
                                 }
                             }

                             ctx->status = HTTP_STATUS_NOT_FOUND;
                             return errorHandler(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    server->registerRoute("*", "/api/v1/friends/client_count", [settingsMgr] (http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_get_friends_client_count(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    server->registerRoute("*", "/api/v1/splatoon/client_count", [settingsMgr] (http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_get_splatoon_client_count(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    server->registerRoute("*", "/api/v1/splatoon/lobby_count", [settingsMgr] (http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_get_splatoon_lobby_count(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    server->registerRoute("*", "/api/v1/splatoon/lobbies", [settingsMgr] (http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_get_splatoon_lobbies(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    server->registerRoute("*", "/api/v1/splatoon/festival_totals", [settingsMgr] (http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_get_festival_totals(srv, std::move(ctx), settingsMgr);
                                 });
                         });

    // Boss management: festivals & map rotation
    server->registerRoute("*", "/api/v1/festivals",
                         [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_POST) {
                                         return mgm_save_festival(srv, std::move(ctx), settingsMgr, mgmDb);
                                     }
                                     return mgm_get_festivals(srv, std::move(ctx), settingsMgr, mgmDb);
                                 });
                         });

    server->registerRoute("*", "/api/v1/festivals/active",
                         [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_get_active_festival(srv, std::move(ctx), settingsMgr, mgmDb);
                                 });
                         });

    server->registerRoute("*", "/api/v1/festivals/switch",
                         [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_switch_active_festival(srv, std::move(ctx), settingsMgr, mgmDb);
                                 });
                         });

    server->registerRegexRoute("*", R"(^/api/v1/festivals/([0-9]+)$)",
                         [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) -> async::Task<void> {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) -> async::Task<void> {
                                     const auto parts = splitPath(ctx->request->getPath());
                                     if (parts.size() < 4) {
                                         return errorHandler(srv, std::move(ctx), settingsMgr);
                                     }
                                     auto id = parseU32(parts[3]);
                                     if (!id) {
                                         ctx->status = HTTP_STATUS_BAD_REQUEST;
                                         return errorHandler(srv, std::move(ctx), settingsMgr);
                                     }
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_GET) {
                                         return mgm_get_festival(srv, std::move(ctx), settingsMgr, mgmDb, static_cast<int>(*id));
                                     }
                                     return mgm_delete_festival(srv, std::move(ctx), settingsMgr, mgmDb, static_cast<int>(*id));
                                 });
                         });

    server->registerRoute("*", "/api/v1/map-rotation",
                         [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     auto method = ctx->request->getMethod();
                                     if (method == http::Method::M_PUT) {
                                         return mgm_update_map_rotation(srv, std::move(ctx), settingsMgr, mgmDb);
                                     }
                                     return mgm_get_map_rotation(srv, std::move(ctx), settingsMgr, mgmDb);
                                 });
                         });

    server->registerRoute("*", "/api/v1/map-rotation/randomize",
                         [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                             return requireAdmin(srv, std::move(ctx), settingsMgr,
                                 [settingsMgr, mgmDb](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                     return mgm_randomize_map_rotation(srv, std::move(ctx), settingsMgr, mgmDb);
                                 });
                         });

    server->registerErrorPage("*",
                             [settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                 return errorHandler(srv, std::move(ctx), settingsMgr);
                             });

    // Ensure default festivals and map rotation exist on startup
    if (mgmDb) {
        server->scheduleArbitraryFunction(initManagementData(mgmDb, logger));
    }
}

} // namespace mgm
