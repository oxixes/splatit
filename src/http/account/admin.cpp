#include "account.hpp"

#include <mailio/message.hpp>
#include <mailio/smtp.hpp>

namespace acc {

using namespace async;

/*
 * Handler for GET https://account.<domain>/v1/api/admin/time
 * Doesn't actually return anything, but the time is in the response headers.
 * Since it's such a simple request, we won't require a device certificate.
 */
Task<void> v1_api_admin_time(const http::Server* srv, std::shared_ptr<http::Context> ctx) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion());
    srv->sendResponse(std::move(ctx), std::move(res), false);
    co_return;
}

/*
 * Handler for GET https://account.<domain>/v1/api/admin/mapped_ids
 * Returns a list of mapped ids for the given input.
 * The input can be either a principal id or a username.
 * Requires a device certificate.
 */
Task<void> v1_api_admin_mapped_ids(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                   std::shared_ptr<db::Database> db,
                                   std::shared_ptr<SettingsManager> settingsManager,
                                   std::shared_ptr<crypto::CertManager> certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!co_await checkRequestParams(ctx->request, settingsManager, certManager, db, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!ctx->request->hasQuery("input_type") || !ctx->request->hasQuery("output_type") || !ctx->request->hasQuery("input")) {
        res = createError(ctx->request->getVersion(), 3, "Request parameters missing", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string inputType = ctx->request->getQuery("input_type");
    std::string outputType = ctx->request->getQuery("output_type");

    if ((inputType != "pid" && inputType != "user_id") || (outputType != "pid" && outputType != "user_id")) {
        res = createError(ctx->request->getVersion(), 1, "Unable to process request", "Bad Request", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::vector<std::string> input = util::split(ctx->request->getQuery("input"), ",");
    // We limit the input to 100 entries, as it would be too much to handle anything more than that
    if (input.empty() || input.size() > 100) {
        res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::vector<ManualTask<db::Result>> tasks;
    for (const auto& id : input) {
        std::unique_ptr<db::Command> cmd;
        if (inputType == "pid") {
            if (id.empty()) continue;

            if (!std::ranges::all_of(id, [](char c) { return std::isdigit(c); })) {
                res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }

            uint32_t pid;
            try {
                pid = std::stoul(id);
            } catch ([[maybe_unused]] const std::out_of_range &e) {
                res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            } catch ([[maybe_unused]] const std::invalid_argument &e) {
                res = createError(ctx->request->getVersion(), 1, "input format is invalid", "input", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }

            cmd = db::Database::craftGetUserByPIDCommand(pid);
        } else {
            cmd = db::Database::craftGetUserByUsernameCommand(id);
        }

        tasks.push_back(std::move(db->runCommand(std::move(cmd))));
    }

    auto results = co_await waitForAll(std::move(tasks));

    pugi::xml_document doc;
    pugi::xml_node mapped_ids = doc.append_child("mapped_ids");

    size_t i = 0;

    for (auto& result : results) {
        if (result.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

        auto userData = result.hasData() ? std::move(result.getData<db::DBUserData>()) : db::DBUserData{};
        auto userPid = result.hasData() ? std::to_string(userData.pid) : inputType == "pid" ? input.at(i) : "";
        auto username = result.hasData() ? userData.username : inputType == "user_id" ? input.at(i) : "";

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

    res = prepareResponse(ctx->request->getVersion(), doc);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

} // namespace acc