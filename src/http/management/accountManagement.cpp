#include "management.hpp"

#include "../../util/util.hpp"
#include "../../crypto/tools.hpp"
#include "../../grpc/asyncRequest.hpp"

#include <limits>
#include <optional>

#include <accountManagement.grpc.pb.h>
#include <google/protobuf/empty.pb.h>

namespace mgm {

using json = nlohmann::json;

namespace {

using grpcimpl::accountmanagement::v1::DevicePlatform;
using grpcimpl::accountmanagement::v1::DeviceRegion;
using grpcimpl::accountmanagement::v1::DeviceStatus;

std::optional<uint32_t> parseU32(const std::string& s) {
    try {
        const unsigned long v = std::stoul(s);
        if (v > std::numeric_limits<uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(v);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<uint64_t> parseU64(const std::string& s) {
    try {
        const unsigned long long v = std::stoull(s);
        return static_cast<uint64_t>(v);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> parseBool(const std::string& s) {
    if (s == "true" || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return std::nullopt;
}

std::optional<DevicePlatform> parseDevicePlatform(const std::string& s) {
    if (s == "WIIU" || s == "wiiu" || s == "wii_u" || s == "WII_U") return DevicePlatform::PLATFORM_WII_U;
    return std::nullopt;
}

std::optional<DeviceRegion> parseDeviceRegion(const std::string& s) {
    if (s == "jpn" || s == "JPN") return DeviceRegion::REGION_JPN;
    if (s == "usa" || s == "USA") return DeviceRegion::REGION_USA;
    if (s == "eur" || s == "EUR") return DeviceRegion::REGION_EUR;
    if (s == "aus" || s == "AUS") return DeviceRegion::REGION_AUS;
    if (s == "chn" || s == "CHN") return DeviceRegion::REGION_CHN;
    if (s == "kor" || s == "KOR") return DeviceRegion::REGION_KOR;
    if (s == "twn" || s == "TWN") return DeviceRegion::REGION_TWN;
    return std::nullopt;
}

std::optional<DeviceStatus> parseDeviceStatus(const std::string& s) {
    if (s == "active" || s == "ACTIVE" || s == "0") return DeviceStatus::STATUS_ACTIVE;
    if (s == "inactive" || s == "INACTIVE" || s == "1") return DeviceStatus::STATUS_INACTIVE;
    return std::nullopt;
}

std::optional<grpcimpl::accountmanagement::v1::AccountGender> parseAccountGender(const std::string& s) {
    if (s == "male" || s == "MALE" || s == "0") return grpcimpl::accountmanagement::v1::AccountGender::GENDER_MALE;
    if (s == "female" || s == "FEMALE" || s == "1") return grpcimpl::accountmanagement::v1::AccountGender::GENDER_FEMALE;
    return std::nullopt;
}

std::string devicePlatformToString(DevicePlatform platform) {
    switch (platform) {
        case DevicePlatform::PLATFORM_WII_U:
            return "WIIU";
        default:
            return "UNKNOWN";
    }
}

std::string deviceRegionToString(DeviceRegion region) {
    switch (region) {
        case DeviceRegion::REGION_JPN:
            return "JPN";
        case DeviceRegion::REGION_USA:
            return "USA";
        case DeviceRegion::REGION_EUR:
            return "EUR";
        case DeviceRegion::REGION_AUS:
            return "AUS";
        case DeviceRegion::REGION_CHN:
            return "CHN";
        case DeviceRegion::REGION_KOR:
            return "KOR";
        case DeviceRegion::REGION_TWN:
            return "TWN";
        default:
            return "UNKNOWN";
    }
}

// Path helpers removed (routing is performed in management.cpp)

json deviceToJson(const grpcimpl::accountmanagement::v1::Device& d) {
    json j;
    j["id"] = d.id();
    j["language"] = d.language();
    j["platform"] = devicePlatformToString(d.platform());
    j["region"] = deviceRegionToString(d.region());
    j["serialNumber"] = d.serialnumber();
    j["systemVersion"] = d.systemversion();
    j["type"] = d.type();
    j["updatedBy"] = d.updatedby();
    j["banned"] = d.banned();
    j["status"] = d.status();
    if (d.has_lastupdated()) {
        j["lastUpdated"] = d.lastupdated().seconds();
    }
    return j;
}

json deviceAttributeToJson(const grpcimpl::accountmanagement::v1::DeviceAttribute& a) {
    json j;
    j["name"] = a.name();
    j["value"] = a.value();
    if (a.has_createdat()) {
        j["createdAt"] = a.createdat().seconds();
    }
    return j;
}

json accountEmailToJson(const grpcimpl::accountmanagement::v1::AccountEmail& e) {
    json j;
    if (e.has_id()) j["id"] = e.id();
    j["address"] = e.address();
    j["parent"] = e.parent();
    j["primary"] = e.primary();
    j["reachable"] = e.reachable();
    j["type"] = e.type();
    j["updatedBy"] = e.updatedby();
    j["validated"] = e.validated();
    j["validationCode"] = e.validationcode();
    if (e.has_validatedat()) j["validatedAt"] = e.validatedat().seconds();
    return j;
}

json accountMiiToJson(const grpcimpl::accountmanagement::v1::AccountMii& m) {
    json j;
    if (m.has_id()) j["id"] = m.id();
    if (m.has_hash()) j["hash"] = m.hash();
    j["name"] = m.name();
    j["primary"] = m.primary();
    j["data"] = m.data();
    return j;
}

json accountAgreementToJson(const grpcimpl::accountmanagement::v1::AccountAgreement& a) {
    json j;
    j["type"] = a.type();
    j["version"] = a.version();
    j["country"] = a.country();
    return j;
}

json accountOwnershipToJson(const grpcimpl::accountmanagement::v1::AccountOwnership& o) {
    json j;
    j["device"] = deviceToJson(o.device());
    j["status"] = o.status();
    j["lastUpdated"] = o.lastupdated().seconds();
    j["attributes"] = json::array();
    for (const auto& a : o.accountdeviceattributes()) {
        j["attributes"].push_back(deviceAttributeToJson(a));
    }
    return j;
}

json accountToJson(const grpcimpl::accountmanagement::v1::Account& a) {
    json j;
    if (a.has_pid()) j["pid"] = a.pid();
    j["username"] = a.username();
    // No devolvemos password.
    j["gender"] = a.gender();
    j["region"] = a.region();
    j["timezone"] = a.timezone();
    j["language"] = a.language();
    j["active"] = a.active();
    j["marketing"] = a.marketing();
    j["offDevice"] = a.offdevice();
    j["birthdate"] = a.birthdate();
    j["country"] = a.country();
    if (a.has_created()) j["created"] = a.created().seconds();
    if (a.has_updated()) j["updated"] = a.updated().seconds();

    j["primaryEmail"] = accountEmailToJson(a.primaryemail());
    j["mii"] = accountMiiToJson(a.mii());

    j["signedAgreements"] = json::array();
    for (const auto& ag : a.signedagreements()) {
        j["signedAgreements"].push_back(accountAgreementToJson(ag));
    }

    j["ownedDevices"] = json::array();
    for (const auto& od : a.owneddevices()) {
        j["ownedDevices"].push_back(accountOwnershipToJson(od));
    }

    return j;
}

json parseJsonBodyOrError(http::Server* srv, std::shared_ptr<http::Context>& ctx, const std::shared_ptr<SettingsManager>& settingsMgr, bool& sent) {
    sent = false;
    try {
        const auto& body = ctx->request->getBody();
        std::string bodyStr(body.begin(), body.end());
        if (bodyStr.empty()) {
            return json::object();
        }
        return json::parse(bodyStr);
    } catch (const std::exception& e) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "Invalid JSON body: " + std::string(e.what()),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        sent = true;
        return json::object();
    }
}

} // namespace

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

            // As long as the server responds, consider it a success
            // (even if the gRPC status is not OK). Errors will be treated in the
            // caller.
            if (response.first != nullptr) {
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

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::GetStoredAgreementsRequest>();
    if (ctx->request->hasQuery("type")) {
        request->set_type(ctx->request->getQuery("type"));
    }

    if (ctx->request->hasQuery("country")) {
        request->set_country(ctx->request->getQuery("country"));
    }

    if (ctx->request->hasQuery("language")) {
        request->set_language(ctx->request->getQuery("language"));
    }

    if (ctx->request->hasQuery("version")) {
        try {
            const int version = std::stoi(ctx->request->getQuery("version"));
            request->set_version(version);
        } catch (const std::exception& e) {
            bool keepAlive = false;
            std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
                "Invalid version parameter: " + std::string(e.what()),
                settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    if (ctx->request->hasQuery("page") && !ctx->request->hasQuery("pageSize")) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
            "pageSize parameter is required when page parameter is provided",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->hasQuery("page")) {
        try {
            const int pageSize = std::stoi(ctx->request->getQuery("pageSize"));
            request->mutable_pagerequest()->set_pagesize(pageSize);
        } catch (const std::exception& e) {
            bool keepAlive = false;
            std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
                "Invalid pageSize parameter: " + std::string(e.what()),
                settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        try {
            const int64_t page = std::stoll(ctx->request->getQuery("page"));
            request->mutable_pagerequest()->set_pagenumber(page);
        } catch (const std::exception& e) {
            bool keepAlive = false;
            std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_REQUEST,
                "Invalid page parameter: " + std::string(e.what()),
                settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    if (ctx->request->hasQuery("sort")) {
        const auto sortParams = util::split(ctx->request->getQuery("sort"), ",");
        for (const auto& param : sortParams) {
            if (param == "type_asc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_TYPE_ASC);
            } else if (param == "type_desc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_TYPE_DESC);
            } else if (param == "version_asc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_VERSION_ASC);
            } else if (param == "version_desc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_VERSION_DESC);
            } else if (param == "country_asc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_COUNTRY_ASC);
            } else if (param == "country_desc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_COUNTRY_DESC);
            } else if (param == "language_asc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_LANGUAGE_ASC);
            } else if (param == "language_desc") {
                request->add_sorting(grpcimpl::accountmanagement::v1::GetStoredAgreementsRequestSorting::SORT_BY_LANGUAGE_DESC);
            }
        }
    }

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::GetStoredAgreementsRequest*,
            grpcimpl::accountmanagement::v1::GetStoredAgreementsResponse*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::GetStoredAgreementsRequest,
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

    responseBody["pagination"] = {
        {"totalItems", response.first->pageresponse().totalitems()},
        {"totalPages", response.first->pageresponse().totalpages()},
        {"currentPage", response.first->pageresponse().currentpage()}
    };

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
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
}

async::Task<void> mgm_list_devices(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
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

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::ListDevicesRequest>();

    if (ctx->request->hasQuery("platform")) {
        auto p = parseDevicePlatform(ctx->request->getQuery("platform"));
        if (!p) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid platform", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_platform(*p);
    }

    if (ctx->request->hasQuery("region")) {
        auto r = parseDeviceRegion(ctx->request->getQuery("region"));
        if (!r) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid region", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_region(*r);
    }

    if (ctx->request->hasQuery("banned")) {
        auto b = parseBool(ctx->request->getQuery("banned"));
        if (!b) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid banned", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_banned(*b);
    }

    if (ctx->request->hasQuery("serialNumber")) {
        request->set_serialnumber(ctx->request->getQuery("serialNumber"));
    }

    if (ctx->request->hasQuery("type")) {
        request->set_type(ctx->request->getQuery("type"));
    }

    if (ctx->request->hasQuery("page") && !ctx->request->hasQuery("pageSize")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST,
            "pageSize parameter is required when page parameter is provided",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->hasQuery("page")) {
        auto pageSize = parseU32(ctx->request->getQuery("pageSize"));
        auto page = parseU64(ctx->request->getQuery("page"));
        if (!pageSize || !page) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid page or pageSize", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->mutable_pagerequest()->set_pagesize(static_cast<int32_t>(*pageSize));
        request->mutable_pagerequest()->set_pagenumber(static_cast<int64_t>(*page));
    }

    if (ctx->request->hasQuery("sort")) {
        const auto sortParams = util::split(ctx->request->getQuery("sort"), ",");
        for (const auto& param : sortParams) {
            if (param == "id_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_ID_ASC);
            else if (param == "id_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_ID_DESC);
            else if (param == "platform_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_PLATFORM_ASC);
            else if (param == "platform_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_PLATFORM_DESC);
            else if (param == "region_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_REGION_ASC);
            else if (param == "region_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_REGION_DESC);
            else if (param == "serial_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_SERIAL_ASC);
            else if (param == "serial_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_SERIAL_DESC);
            else if (param == "lastUpdated_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_LAST_UPDATED_ASC);
            else if (param == "lastUpdated_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListDevicesRequestSorting::SORT_DEVICE_BY_LAST_UPDATED_DESC);
        }
    }

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::ListDevicesRequest*,
            grpcimpl::accountmanagement::v1::ListDevicesResponse*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::ListDevicesRequest,
        grpcimpl::accountmanagement::v1::ListDevicesResponse
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::ListDevices,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to list devices: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["devices"] = json::array();
    for (const auto& d : response.first->devices()) {
        responseBody["devices"].push_back(deviceToJson(d));
    }
    responseBody["pagination"] = {
        {"totalItems", response.first->pageresponse().totalitems()},
        {"totalPages", response.first->pageresponse().totalpages()},
        {"currentPage", response.first->pageresponse().currentpage()}
    };

    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_create_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_POST && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    const std::vector<std::string> required = {"language", "platform", "region", "serialNumber", "systemVersion", "type", "updatedBy", "status"};
    for (const auto& k : required) {
        if (!body.contains(k)) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Missing required field: " + k, settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    auto platform = parseDevicePlatform(body["platform"].get<std::string>());
    auto region = parseDeviceRegion(body["region"].get<std::string>());
    if (!platform || !region) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid platform or region", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::DeviceCreate>();
    request->set_language(body["language"].get<std::string>());
    request->set_platform(*platform);
    request->set_region(*region);
    request->set_serialnumber(body["serialNumber"].get<std::string>());
    request->set_systemversion(body["systemVersion"].get<std::string>());
    request->set_type(body["type"].get<std::string>());
    request->set_updatedby(body["updatedBy"].get<std::string>());
    request->set_status(body["status"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::DeviceCreate*,
            grpcimpl::accountmanagement::v1::Device*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::DeviceCreate,
        grpcimpl::accountmanagement::v1::Device
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::CreateDevice,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to create device: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["device"] = deviceToJson(*response.first);

    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_CREATED);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_get_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t deviceId) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::DeviceGetRequest>();
    request->set_id(deviceId);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::DeviceGetRequest*,
            grpcimpl::accountmanagement::v1::Device*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::DeviceGetRequest,
        grpcimpl::accountmanagement::v1::Device
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::GetDevice,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Device not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to get device: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["device"] = deviceToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_update_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t deviceId) {
    if (ctx->request->getMethod() != http::Method::M_PUT && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "PUT, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::DeviceUpdate>();
    request->set_id(deviceId);

    if (body.contains("language")) request->set_language(body["language"].get<std::string>());
    if (body.contains("platform")) {
        auto p = parseDevicePlatform(body["platform"].get<std::string>());
        if (!p) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid platform", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_platform(*p);
    }
    if (body.contains("region")) {
        auto r = parseDeviceRegion(body["region"].get<std::string>());
        if (!r) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid region", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_region(*r);
    }
    if (body.contains("serialNumber")) request->set_serialnumber(body["serialNumber"].get<std::string>());
    if (body.contains("systemVersion")) request->set_systemversion(body["systemVersion"].get<std::string>());
    if (body.contains("type")) request->set_type(body["type"].get<std::string>());
    if (body.contains("updatedBy")) request->set_updatedby(body["updatedBy"].get<std::string>());
    if (body.contains("banned")) request->set_banned(body["banned"].get<bool>());
    if (body.contains("status")) request->set_status(body["status"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::DeviceUpdate*,
            grpcimpl::accountmanagement::v1::Device*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::DeviceUpdate,
        grpcimpl::accountmanagement::v1::Device
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::UpdateDevice,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Device not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to update device: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["device"] = deviceToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_delete_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t deviceId) {
    if (ctx->request->getMethod() != http::Method::M_DELETE && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "DELETE, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::DeviceDeleteRequest>();
    request->set_id(deviceId);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::DeviceDeleteRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::DeviceDeleteRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::DeleteDevice,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to delete device: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_list_accounts(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::ListAccountsRequest>();

    if (ctx->request->hasQuery("username")) request->set_username(ctx->request->getQuery("username"));
    if (ctx->request->hasQuery("gender")) {
        auto g = parseAccountGender(ctx->request->getQuery("gender"));
        if (!g) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid gender", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_gender(*g);
    }
    if (ctx->request->hasQuery("region")) {
        auto r = parseU64(ctx->request->getQuery("region"));
        if (!r) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid region", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_region(*r);
    }
    if (ctx->request->hasQuery("active")) {
        auto a = parseBool(ctx->request->getQuery("active"));
        if (!a) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid active", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_active(*a);
    }

    if (ctx->request->hasQuery("page") && !ctx->request->hasQuery("pageSize")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST,
            "pageSize parameter is required when page parameter is provided",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->hasQuery("page")) {
        auto pageSize = parseU32(ctx->request->getQuery("pageSize"));
        auto page = parseU64(ctx->request->getQuery("page"));
        if (!pageSize || !page) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid page or pageSize", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->mutable_pagerequest()->set_pagesize(static_cast<int32_t>(*pageSize));
        request->mutable_pagerequest()->set_pagenumber(static_cast<int64_t>(*page));
    }

    if (ctx->request->hasQuery("sort")) {
        const auto sortParams = util::split(ctx->request->getQuery("sort"), ",");
        for (const auto& param : sortParams) {
            if (param == "pid_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListAccountsRequestSorting::SORT_ACCOUNT_BY_PID_ASC);
            else if (param == "pid_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListAccountsRequestSorting::SORT_ACCOUNT_BY_PID_DESC);
            else if (param == "username_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListAccountsRequestSorting::SORT_ACCOUNT_BY_USERNAME_ASC);
            else if (param == "username_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListAccountsRequestSorting::SORT_ACCOUNT_BY_USERNAME_DESC);
            else if (param == "region_asc") request->add_sorting(grpcimpl::accountmanagement::v1::ListAccountsRequestSorting::SORT_ACCOUNT_BY_REGION_ASC);
            else if (param == "region_desc") request->add_sorting(grpcimpl::accountmanagement::v1::ListAccountsRequestSorting::SORT_ACCOUNT_BY_REGION_DESC);
        }
    }

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::ListAccountsRequest*,
            grpcimpl::accountmanagement::v1::ListAccountsResponse*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::ListAccountsRequest,
        grpcimpl::accountmanagement::v1::ListAccountsResponse
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::ListAccounts,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to list accounts: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["accounts"] = json::array();
    for (const auto& a : response.first->accounts()) {
        responseBody["accounts"].push_back(accountToJson(a));
    }
    responseBody["pagination"] = {
        {"totalItems", response.first->pageresponse().totalitems()},
        {"totalPages", response.first->pageresponse().totalpages()},
        {"currentPage", response.first->pageresponse().currentpage()}
    };

    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_create_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_POST && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    const std::vector<std::string> required = {"username", "password", "gender", "region", "timezone", "language", "email", "mii"};
    for (const auto& k : required) {
        if (!body.contains(k)) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Missing required field: " + k, settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    auto gender = parseAccountGender(body["gender"].get<std::string>());
    if (!gender) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid gender", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto region = body["region"].is_string() ? parseU64(body["region"].get<std::string>()) : std::optional<uint64_t>(body["region"].get<uint64_t>());
    if (!region) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid region", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!body["email"].is_object() || !body["mii"].is_object()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "email and mii must be objects", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Create protobuf request
    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AccountCreate>();
    request->set_username(body["username"].get<std::string>());
    request->set_password(body["password"].get<std::string>());
    request->set_gender(*gender);
    request->set_region(*region);
    request->set_timezone(body["timezone"].get<std::string>());
    request->set_language(body["language"].get<std::string>());
    request->set_marketing(body.value("marketing", false));
    request->set_offdevice(body.value("offDevice", false));
    request->set_birthdate(body.value("birthdate", std::string("")));
    request->set_country(body.value("country", std::string("")));

    // email
    auto* email = request->mutable_email();
    const auto& emailJson = body["email"];
    if (!emailJson.contains("address")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "email.address is required", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    email->set_address(emailJson["address"].get<std::string>());
    email->set_parent(emailJson.value("parent", false));
    email->set_primary(emailJson.value("primary", true));
    email->set_reachable(emailJson.value("reachable", true));
    email->set_type(emailJson.value("type", std::string("unknown")));
    email->set_updatedby(emailJson.value("updatedBy", std::string("management")));
    email->set_validated(emailJson.value("validated", false));

    // mii
    auto* mii = request->mutable_mii();
    const auto& miiJson = body["mii"];
    if (!miiJson.contains("name") || !miiJson.contains("data")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "mii.name and mii.data are required", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    mii->set_name(miiJson["name"].get<std::string>());
    mii->set_primary(miiJson.value("primary", true));
    mii->set_data(miiJson["data"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AccountCreate*,
            grpcimpl::accountmanagement::v1::Account*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AccountCreate,
        grpcimpl::accountmanagement::v1::Account
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::CreateAccount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        if (response.second.error_code() == grpc::StatusCode::ALREADY_EXISTS) {
            auto res = createError(ctx, ManagementError::CONFLICT, "Account with the given username already exists", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_CONFLICT);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        } else {
            auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to create account: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        }
        co_return;
    }

    json responseBody;
    responseBody["account"] = accountToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_CREATED);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_get_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AccountGetRequest>();
    request->set_pid(pid);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AccountGetRequest*,
            grpcimpl::accountmanagement::v1::Account*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AccountGetRequest,
        grpcimpl::accountmanagement::v1::Account
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::GetAccount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to get account: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["account"] = accountToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_get_account_by_username(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    if (!ctx->request->hasQuery("username")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "username query parameter is required", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AccountGetByUsernameRequest>();
    request->set_username(ctx->request->getQuery("username"));

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AccountGetByUsernameRequest*,
            grpcimpl::accountmanagement::v1::Account*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AccountGetByUsernameRequest,
        grpcimpl::accountmanagement::v1::Account
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::GetAccountByUsername,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to get account by username: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["account"] = accountToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_update_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_PUT && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, PUT, DELETE, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AccountUpdate>();
    request->set_pid(pid);

    if (body.contains("username")) request->set_username(body["username"].get<std::string>());
    if (body.contains("password")) request->set_password(body["password"].get<std::string>());
    if (body.contains("gender")) {
        auto g = parseAccountGender(body["gender"].get<std::string>());
        if (!g) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid gender", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_gender(*g);
    }
    if (body.contains("region")) {
        auto r = body["region"].is_string() ? parseU64(body["region"].get<std::string>()) : std::optional<uint64_t>(body["region"].get<uint64_t>());
        if (!r) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid region", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        request->set_region(*r);
    }
    if (body.contains("timezone")) request->set_timezone(body["timezone"].get<std::string>());
    if (body.contains("language")) request->set_language(body["language"].get<std::string>());
    if (body.contains("active")) request->set_active(body["active"].get<bool>());
    if (body.contains("marketing")) request->set_marketing(body["marketing"].get<bool>());
    if (body.contains("offDevice")) request->set_offdevice(body["offDevice"].get<bool>());
    if (body.contains("birthdate")) request->set_birthdate(body["birthdate"].get<std::string>());
    if (body.contains("country")) request->set_country(body["country"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AccountUpdate*,
            grpcimpl::accountmanagement::v1::Account*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AccountUpdate,
        grpcimpl::accountmanagement::v1::Account
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::UpdateAccount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::ALREADY_EXISTS) {
            auto res = createError(ctx, ManagementError::CONFLICT, "Account with the given username already exists", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_CONFLICT);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        } else if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        } else {
            auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to update account: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
            srv->sendResponse(std::move(ctx), std::move(res), false);
        }
        co_return;
    }

    json responseBody;
    responseBody["account"] = accountToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_update_account_email(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_PUT && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "PUT, OPTIONS", keepAlive);
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

    if (!requestBody.contains("emailId")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "emailId is required", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto emailId = requestBody["emailId"].is_string() ? parseU32(requestBody["emailId"].get<std::string>()) : std::optional<uint32_t>(requestBody["emailId"].get<uint32_t>());
    if (!emailId) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid emailId", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::UpdateAccountEmailRequest>();
    request->set_pid(pid);
    request->set_emailid(*emailId);

    if (requestBody.contains("address")) request->set_address(requestBody["address"].get<std::string>());
    if (requestBody.contains("parent")) request->set_parent(requestBody["parent"].get<bool>());
    if (requestBody.contains("primary")) request->set_primary(requestBody["primary"].get<bool>());
    if (requestBody.contains("reachable")) request->set_reachable(requestBody["reachable"].get<bool>());
    if (requestBody.contains("type")) request->set_type(requestBody["type"].get<std::string>());
    if (requestBody.contains("updatedBy")) request->set_updatedby(requestBody["updatedBy"].get<std::string>());
    if (requestBody.contains("validated")) request->set_validated(requestBody["validated"].get<bool>());
    if (requestBody.contains("validationCode")) request->set_validationcode(requestBody["validationCode"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::UpdateAccountEmailRequest*,
            grpcimpl::accountmanagement::v1::AccountEmail*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::UpdateAccountEmailRequest,
        grpcimpl::accountmanagement::v1::AccountEmail
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::UpdateAccountEmail,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account or email not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to update account email: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["email"] = accountEmailToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_set_account_mii(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_PUT && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "PUT, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    const std::vector<std::string> required = {"name", "primary", "data"};
    for (const auto& k : required) {
        if (!body.contains(k)) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Missing required field: " + k, settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::SetAccountMiiRequest>();
    request->set_pid(pid);
    request->set_name(body["name"].get<std::string>());
    request->set_primary(body["primary"].get<bool>());
    request->set_data(body["data"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::SetAccountMiiRequest*,
            grpcimpl::accountmanagement::v1::AccountMii*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::SetAccountMiiRequest,
        grpcimpl::accountmanagement::v1::AccountMii
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::SetAccountMii,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to set account mii: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["mii"] = accountMiiToJson(*response.first);
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_add_account_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_POST && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    const std::vector<std::string> required = {"type", "version", "country"};
    for (const auto& k : required) {
        if (!body.contains(k)) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Missing required field: " + k, settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AddAccountAgreementRequest>();
    request->set_pid(pid);
    request->set_type(body["type"].get<std::string>());
    request->set_version(body["version"].get<uint32_t>());
    request->set_country(body["country"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AddAccountAgreementRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AddAccountAgreementRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::AddAccountAgreement,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account does not exist", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to add account agreement: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_CREATED);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_remove_account_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_DELETE && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "POST, DELETE, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    const std::vector<std::string> required = {"type", "version", "country"};
    for (const auto& k : required) {
        if (!body.contains(k)) {
            bool keepAlive = false;
            auto res = createError(ctx, ManagementError::BAD_REQUEST, "Missing required field: " + k, settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::RemoveAccountAgreementRequest>();
    request->set_pid(pid);
    request->set_type(body["type"].get<std::string>());
    request->set_version(body["version"].get<uint32_t>());
    request->set_country(body["country"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::RemoveAccountAgreementRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::RemoveAccountAgreementRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::RemoveAccountAgreement,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to remove account agreement: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_link_device_to_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_POST && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    if (!body.contains("deviceId") || !body.contains("status")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "deviceId and status are required", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto deviceId = body["deviceId"].is_string() ? parseU32(body["deviceId"].get<std::string>()) : std::optional<uint32_t>(body["deviceId"].get<uint32_t>());
    auto status = parseDeviceStatus(body["status"].get<std::string>());

    if (!deviceId || !status) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid deviceId or status", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::LinkDeviceRequest>();
    request->set_pid(pid);
    request->set_deviceid(*deviceId);
    request->set_status(*status);

    if (body.contains("attributes") && body["attributes"].is_array()) {
        for (const auto& a : body["attributes"]) {
            if (!a.is_object() || !a.contains("name") || !a.contains("value")) continue;
            auto* attr = request->add_accountdeviceattributes();
            attr->set_name(a["name"].get<std::string>());
            attr->set_value(a["value"].get<std::string>());
        }
    }

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::LinkDeviceRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::LinkDeviceRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::LinkDeviceToAccount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to link device to account: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_CREATED);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_unlink_device_from_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId) {
    if (ctx->request->getMethod() != http::Method::M_DELETE && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "DELETE, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::UnlinkDeviceRequest>();
    request->set_pid(pid);
    request->set_deviceid(deviceId);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::UnlinkDeviceRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::UnlinkDeviceRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::UnlinkDeviceFromAccount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Ownership not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to unlink device: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_update_account_device_status(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId) {
    if (ctx->request->getMethod() != http::Method::M_PUT && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "PUT, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    if (!body.contains("status")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "status is required", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto status = parseDeviceStatus(body["status"].get<std::string>());
    if (!status) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Invalid status", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::UpdateDeviceStatusRequest>();
    request->set_pid(pid);
    request->set_deviceid(deviceId);
    request->set_status(*status);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::UpdateDeviceStatusRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::UpdateDeviceStatusRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::UpdateDeviceStatus,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Ownership not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to update device status: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_list_account_device_attributes(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId) {
    if (ctx->request->getMethod() != http::Method::M_GET && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::ListAccountDeviceAttributesRequest>();
    request->set_pid(pid);
    request->set_deviceid(deviceId);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::ListAccountDeviceAttributesRequest*,
            grpcimpl::accountmanagement::v1::ListAccountDeviceAttributesResponse*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::ListAccountDeviceAttributesRequest,
        grpcimpl::accountmanagement::v1::ListAccountDeviceAttributesResponse
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::ListAccountDeviceAttributes,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to list attributes: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody;
    responseBody["attributes"] = json::array();
    for (const auto& a : response.first->attributes()) {
        responseBody["attributes"].push_back(deviceAttributeToJson(a));
    }

    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_set_account_device_attribute(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId, const std::string& attributeName) {
    if (ctx->request->getMethod() != http::Method::M_PUT && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "PUT, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    if (!body.contains("value")) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "value is required", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::SetAccountDeviceAttributeRequest>();
    request->set_pid(pid);
    request->set_deviceid(deviceId);
    request->set_attributename(attributeName);
    request->set_attributevalue(body["value"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::SetAccountDeviceAttributeRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::SetAccountDeviceAttributeRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::SetAccountDeviceAttribute,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to set attribute: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_remove_account_device_attribute(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId, const std::string& attributeName) {
    if (ctx->request->getMethod() != http::Method::M_DELETE && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }
    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "PUT, DELETE, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::RemoveAccountDeviceAttributeRequest>();
    request->set_pid(pid);
    request->set_deviceid(deviceId);
    request->set_attributename(attributeName);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::RemoveAccountDeviceAttributeRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::RemoveAccountDeviceAttributeRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::RemoveAccountDeviceAttribute,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to remove attribute: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_get_cemu_files(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
    if (ctx->request->getMethod() != http::Method::M_POST && ctx->request->getMethod() != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (ctx->request->getMethod() == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        auto res = prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive);
        srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
        co_return;
    }

    bool sent = false;
    json body = parseJsonBodyOrError(srv, ctx, settingsMgr, sent);
    if (sent) co_return;

    if (!body.contains("password") || !body["password"].is_string()) {
        bool keepAlive = false;
        auto res = createError(ctx, ManagementError::BAD_REQUEST, "Missing or invalid password field", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::CemuFilesGetRequest>();
    request->set_pid(pid);
    request->set_password(body["password"].get<std::string>());

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::CemuFilesGetRequest*,
            grpcimpl::accountmanagement::v1::CemuFilesResponse*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::CemuFilesGetRequest,
        grpcimpl::accountmanagement::v1::CemuFilesResponse
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::GetCemuFiles,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;

        if (response.second.error_code() == grpc::StatusCode::NOT_FOUND) {
            auto res = createError(ctx, ManagementError::NOT_FOUND, "Account or device not found", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        if (response.second.error_code() == grpc::StatusCode::PERMISSION_DENIED) {
            auto res = createError(ctx, ManagementError::PERMISSION_DENIED, "Invalid password", settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_UNAUTHORIZED);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        if (response.second.error_code() == grpc::StatusCode::FAILED_PRECONDITION) {
            auto res = createError(ctx, ManagementError::INTERNAL_ERROR, response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto res = createError(ctx, ManagementError::BAD_GATEWAY, "Failed to get CEMU files: " + response.second.error_message(), settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Convert binary data to base64 for JSON response
    const auto& cemuFiles = *response.first;
    
    json responseBody;
    responseBody["accountDat"] = crypto::base64Encode(std::vector<uint8_t>(cemuFiles.accountdat().begin(), cemuFiles.accountdat().end()));
    responseBody["otp"] = crypto::base64Encode(std::vector<uint8_t>(cemuFiles.otp().begin(), cemuFiles.otp().end()));
    responseBody["seeprom"] = crypto::base64Encode(std::vector<uint8_t>(cemuFiles.seeprom().begin(), cemuFiles.seeprom().end()));
    responseBody["clientCert"] = crypto::base64Encode(std::vector<uint8_t>(cemuFiles.clientcert().begin(), cemuFiles.clientcert().end()));
    responseBody["clientKey"] = crypto::base64Encode(std::vector<uint8_t>(cemuFiles.clientkey().begin(), cemuFiles.clientkey().end()));
    responseBody["serverCert"] = crypto::base64Encode(std::vector<uint8_t>(cemuFiles.servercert().begin(), cemuFiles.servercert().end()));
    responseBody["networkServices"] = crypto::base64Encode(std::vector<uint8_t>(cemuFiles.networkservices().begin(), cemuFiles.networkservices().end()));
    responseBody["persistentId"] = cemuFiles.persistentid();

    bool keepAlive = false;
    auto res = prepareResponse(ctx, responseBody, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
}

async::Task<void> mgm_delete_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid) {
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

    auto request = std::make_shared<grpcimpl::accountmanagement::v1::AccountDeleteRequest>();
    request->set_pid(pid);

    auto response = co_await callAccountServerWithFallback<
        grpcimpl::accountmanagement::v1::AccountManagementService,
        void (grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::*)(
            grpc::ClientContext*,
            const grpcimpl::accountmanagement::v1::AccountDeleteRequest*,
            google::protobuf::Empty*,
            std::function<void(grpc::Status)>
        ),
        grpcimpl::accountmanagement::v1::AccountDeleteRequest,
        google::protobuf::Empty
    >(
        ctx,
        &grpcimpl::accountmanagement::v1::AccountManagementService::Stub::async::DeleteAccount,
        request,
        settingsMgr->getManagementgRPCRequestTimeout()
    );

    if (!response.second.ok()) {
        bool keepAlive = false;
        std::unique_ptr<http::Response> res = createError(ctx, ManagementError::BAD_GATEWAY,
            "Failed to delete account: " + response.second.error_message(),
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    bool keepAlive = false;
    std::unique_ptr<http::Response> res = prepareResponse(ctx, keepAlive, settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_NO_CONTENT);
    srv->sendResponse(std::move(ctx), std::move(res), keepAlive);
    co_return;
}

} // namespace mgm

