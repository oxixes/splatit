#include "account.hpp"

namespace acc {

using namespace async;

/*
 * Handler for GET https://account.<domain>/v1/api/devices/@current/status
 * Returns the current status of the device.
 */
Task<void> v1_api_devices_current_status(http::Server* srv, std::shared_ptr<http::Context> ctx,
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

    uint32_t deviceId = std::stoul(ctx->request->getHeader("x-nintendo-device-id")[0]); // Was checked in checkRequestParams
    auto cmd = db::Database::craftGetDeviceCommand(deviceId);
    db::Result result = co_await db->runCommand(std::move(cmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

    std::string status = "ACTIVE";
    if (result.hasData()) {
        auto deviceData = result.getData<db::DBDeviceData>();
        status = deviceData.status;
    }

    if (status != "ACTIVE") {
        res = createError(ctx->request->getVersion(), 143, "Device is not active", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    pugi::xml_document doc;
    doc.append_child("device");
    res = prepareResponse(ctx->request->getVersion(), doc);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for PUT https://account.<domain>/v1/api/devices/@current/inactivate
 * Inactivates the current device and all associations with accounts.
 */
Task<void> v1_api_devices_current_inactivate(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                            std::shared_ptr<db::Database> db,
                                            std::shared_ptr<SettingsManager> settingsManager,
                                            std::shared_ptr<CertManager> certManager) {
    if (ctx->request->getMethod() != http::Method::M_PUT) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    uint32_t deviceId = std::stoul(ctx->request->getHeader("x-nintendo-device-id")[0]); // Was checked in checkRequestParams

    auto cmd = db::Database::craftGetDeviceCommand(deviceId);
    db::Result result = co_await db->runCommand(std::move(cmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

    if (!result.hasData()) {
        // Real server should have all devices registered, so if we don't find it, just return 200
        res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto deviceData = result.getData<db::DBDeviceData>();

    std::shared_ptr<db::Database> session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    auto ownershipCmd = db::Database::craftInactivateDeviceOwnershipsCommand(deviceId);
    db::Result ownershipResult = co_await session->runCommand(std::move(ownershipCmd));
    if (ownershipResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        throw std::runtime_error("Database error");
    }

    db::datetime_t lastUpdated = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    auto updateCmd = db::Database::craftInsertOrUpdateDeviceCommand(
        deviceData.deviceId, deviceData.language, deviceData.platformId, deviceData.region,
        deviceData.serialNumber, deviceData.systemVersion, deviceData.type, deviceData.updatedBy,
        "INACTIVE", lastUpdated); // Set status to INACTIVE
    db::Result updateResult = co_await session->runCommand(std::move(updateCmd));
    if (updateResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        throw std::runtime_error("Database error");
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

} // namespace acc