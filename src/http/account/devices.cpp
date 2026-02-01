#include "account.hpp"

namespace acc {

using namespace async;

/*
 * Handler for GET https://account.<domain>/v1/api/devices/@current/status
 * Returns the current status of the device.
 *
 * The real server ALWAYS returns "<device/>". Even if you use a fake or unregistered device ID
 * (literally anything in the x-nintendo-device-id or x-nintendo-serial-number header), it still returns "<device/>".
 */
Task<void> v1_api_devices_current_status(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                         std::shared_ptr<db::Database> db,
                                         std::shared_ptr<SettingsManager> settingsManager,
                                         std::shared_ptr<crypto::CertManager> certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!ctx->request->hasHeader("x-nintendo-device-id")) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 3, "Missing header", "x-nintendo-device-id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    uint32_t deviceId = 0;
    try {
        deviceId = std::stoul(ctx->request->getHeader("x-nintendo-device-id")[0]);
    } catch (const std::exception&) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 2, "Invalid header value", "x-nintendo-device-id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (co_await checkDeviceBanned(deviceId, db)) {
        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                         "Banned device with ID " + std::to_string(deviceId) + " tried to generate an access token (client " + util::ipv4ToString(ctx->client) + ").");
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 12, "The device is banned", "", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
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
 * Inactivates the current device.
 */
Task<void> v1_api_devices_current_inactivate(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                            std::shared_ptr<db::Database> db,
                                            std::shared_ptr<SettingsManager> settingsManager,
                                            std::shared_ptr<crypto::CertManager> certManager) {
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

    db::datetime_t lastUpdated = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    auto updateCmd = db::Database::craftInsertOrUpdateDeviceCommand(
        deviceData.deviceId, deviceData.language, deviceData.platformId, deviceData.region,
        deviceData.serialNumber, deviceData.systemVersion, deviceData.type, deviceData.updatedBy,
        "INACTIVE", deviceData.banned, lastUpdated); // Set status to INACTIVE
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