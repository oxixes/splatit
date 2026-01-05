#include "management.hpp"

#include "../../util/util.hpp"
#include "../../grpc/asyncRequest.hpp"
#include <google/protobuf/empty.pb.h>
#include "../../../cmake-build-debug/generated/accountManagement.grpc.pb.h"

namespace mgm {

/*
 * Helper function to try connecting to account servers in round-robin fashion
 * Returns a pair of channel and the address used (empty if all failed)
 */
std::pair<std::shared_ptr<grpc::Channel>, sock::IPv4Addr> getAccountServerChannel() {
    auto it = serverHosts.find(ServerType::ACCOUNT);
    if (it == serverHosts.end() || it->second.empty()) {
        return {nullptr, sock::IPv4Addr{}};
    }

    const auto& hostList = it->second;
    size_t startIndex = serverHostsIndexRoundRobin[ServerType::ACCOUNT];
    size_t currentIndex = startIndex;

    // Try all servers in round-robin fashion
    do {
        const auto& host = hostList[currentIndex];
        auto channel = channelPool->getChannel(util::ipv4WPortToString(host));

        if (channel) {
            // Update the round-robin index for next time
            serverHostsIndexRoundRobin[ServerType::ACCOUNT] = (currentIndex + 1) % hostList.size();
            return {channel, host};
        }

        currentIndex = (currentIndex + 1) % hostList.size();
    } while (currentIndex != startIndex);

    return {nullptr, sock::IPv4Addr{}};
}

/*
 * Helper function to try a gRPC request with fallback to other account servers
 */
template <typename Service, typename Method, typename Request, typename Response>
async::Task<std::pair<std::shared_ptr<Response>, grpc::Status>> callAccountServerWithFallback(
    const std::shared_ptr<http::Context>& ctx,
    Method method,
    std::shared_ptr<Request> request,
    int timeoutMs
) {
    auto it = serverHosts.find(ServerType::ACCOUNT);
    if (it == serverHosts.end() || it->second.empty()) {
        grpc::Status status(grpc::StatusCode::UNAVAILABLE, "No account servers configured");
        co_return std::make_pair(nullptr, status);
    }

    const auto& hostList = it->second;
    size_t startIndex = serverHostsIndexRoundRobin[ServerType::ACCOUNT];
    size_t currentIndex = startIndex;

    // Try all servers
    do {
        const auto& host = hostList[currentIndex];
        ctx->logger->log(Logger::level::DEBUG, Logger::group::MANAGEMENT,
                        "Trying account server at " + util::ipv4WPortToString(host));

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

            if (response.second.ok()) {
                // Success! Update round-robin index
                serverHostsIndexRoundRobin[ServerType::ACCOUNT] = (currentIndex + 1) % hostList.size();
                co_return response;
            }

            ctx->logger->log(Logger::level::WARN, Logger::group::MANAGEMENT,
                            "Failed to contact account server at " + util::ipv4WPortToString(host) +
                            ": " + response.second.error_message());
        } else {
            ctx->logger->log(Logger::level::WARN, Logger::group::MANAGEMENT,
                            "Failed to create channel to account server at " + util::ipv4WPortToString(host));
        }

        currentIndex = (currentIndex + 1) % hostList.size();
    } while (currentIndex != startIndex);

    // All servers failed
    grpc::Status status(grpc::StatusCode::UNAVAILABLE, "All account servers unreachable");
    co_return std::make_pair(nullptr, status);
}

/*
 * Handler for GET /api/v1/agreements
 *
 * Retrieves all stored agreements from the account server.
 */
async::Task<void> mgm_get_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, POST, DELETE, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<google::protobuf::Empty>();

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const google::protobuf::Empty*,
            grpcimpl::accountmanagement::v1::GetStoredAgreementsResponse*,
            std::function<void(grpc::Status)>
        ),
        google::protobuf::Empty,
        grpcimpl::accountmanagement::v1::GetStoredAgreementsResponse
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::GetStoredAgreements,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to contact account servers: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Convert protobuf response to JSON
    json responseBody;
    responseBody["agreements"] = json::array();

    for (const auto& agreement : response.first->agreements()) {
        json agreementJson;
        agreementJson["type"] = agreement.createinfo().type();
        agreementJson["version"] = agreement.createinfo().version();
        agreementJson["country"] = agreement.createinfo().country();
        agreementJson["language"] = agreement.createinfo().language();
        agreementJson["languageName"] = agreement.createinfo().languagename();
        agreementJson["mainTitle"] = agreement.createinfo().maintitle();
        agreementJson["mainContent"] = agreement.createinfo().maincontent();
        agreementJson["subTitle"] = agreement.createinfo().subtitle();
        agreementJson["subContent"] = agreement.createinfo().subcontent();
        agreementJson["agreeButtonText"] = agreement.createinfo().agreebuttontext();
        agreementJson["disagreeButtonText"] = agreement.createinfo().disagreebuttontext();
        agreementJson["publishDate"] = agreement.publishdate().seconds();

        responseBody["agreements"].push_back(agreementJson);
    }

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
    co_return;
}

/*
 * Handler for POST /api/v1/agreements
 *
 * Publishes a new agreement to the account server.
 */
async::Task<void> mgm_publish_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
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

    // Parse JSON body
    json requestBody;
    try {
        const auto& body = ctx->request->getBody();
        std::string bodyStr(body.begin(), body.end());
        requestBody = json::parse(bodyStr);
    } catch (const std::exception& e) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "Invalid JSON body: " + std::string(e.what()),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Validate required fields
    if (!requestBody.contains("type") || !requestBody.contains("version") ||
        !requestBody.contains("country") || !requestBody.contains("language")) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "Missing required fields: type, version, country, language",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Create protobuf request
    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AgreementCreate>();
    request->set_type(requestBody["type"].get<std::string>());
    request->set_version(requestBody["version"].get<uint32_t>());
    request->set_country(requestBody["country"].get<std::string>());
    request->set_language(requestBody["language"].get<std::string>());

    if (requestBody.contains("languageName")) {
        request->set_languagename(requestBody["languageName"].get<std::string>());
    }
    if (requestBody.contains("mainTitle")) {
        request->set_maintitle(requestBody["mainTitle"].get<std::string>());
    }
    if (requestBody.contains("mainContent")) {
        request->set_maincontent(requestBody["mainContent"].get<std::string>());
    }
    if (requestBody.contains("subTitle")) {
        request->set_subtitle(requestBody["subTitle"].get<std::string>());
    }
    if (requestBody.contains("subContent")) {
        request->set_subcontent(requestBody["subContent"].get<std::string>());
    }
    if (requestBody.contains("agreeButtonText")) {
        request->set_agreebuttontext(requestBody["agreeButtonText"].get<std::string>());
    }
    if (requestBody.contains("disagreeButtonText")) {
        request->set_disagreebuttontext(requestBody["disagreeButtonText"].get<std::string>());
    }

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AgreementCreate*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AgreementCreate,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::PublishAgreement,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to publish agreement: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {
        {"status", "ok"},
        {"message", "Agreement published successfully"}
    };

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_CREATED);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
    co_return;
}

/*
 * Handler for DELETE /api/v1/agreements
 *
 * Deletes an existing agreement from the account server.
 */
async::Task<void> mgm_delete_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_DELETE && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = prepareCORSPreflightResponse(ctx, settingsMgr, "DELETE, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    // Parse JSON body
    json requestBody;
    try {
        const auto& body = ctx->request->getBody();
        std::string bodyStr(body.begin(), body.end());
        requestBody = json::parse(bodyStr);
    } catch (const std::exception& e) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "Invalid JSON body: " + std::string(e.what()),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Validate required fields
    if (!requestBody.contains("type") || !requestBody.contains("country") || !requestBody.contains("language")) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "Missing required fields: type, country, language",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Create protobuf request
    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AgreementDelete>();
    request->set_type(requestBody["type"].get<std::string>());
    request->set_country(requestBody["country"].get<std::string>());
    request->set_language(requestBody["language"].get<std::string>());

    if (requestBody.contains("version")) {
        request->set_version(requestBody["version"].get<uint32_t>());
    }

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AgreementDelete*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AgreementDelete,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::DeleteAgreement,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to delete agreement: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {
        {"status", "ok"},
        {"message", "Agreement deleted successfully"}
    };

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
    co_return;
}

} // namespace mgm

