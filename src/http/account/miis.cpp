#include "account.hpp"

namespace acc {

using namespace async;

/*
 * Handler for GET https://account.<domain>/v1/api/miis
 * Obtains the miis associated with the given principal ids.
 */
Task<void> v1_api_miis(http::Server* srv, std::shared_ptr<http::Context> ctx,
                       std::shared_ptr<db::Database> db,
                       std::shared_ptr<SettingsManager> settingsManager,
                       std::shared_ptr<crypto::CertManager> certManager) {
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

    if (!ctx->request->hasQuery("pids")) {
        res = createError(ctx->request->getVersion(), 3, "Request parameters missing", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::vector<std::string> input = util::split(ctx->request->getQuery("pids"), ",");
    // We limit the input to 100 entries, as it would be too much to handle anything more than that
    if (input.empty() || input.size() > 100) {
        res = createError(ctx->request->getVersion(), 1, "input format is invalid", "pids", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::vector<ManualTask<db::Result>> tasks;
    for (const auto& pidStr : input) {
        uint32_t pid;
        try {
            pid = std::stoul(pidStr);
        } catch (const std::invalid_argument& e) {
            res = createError(ctx->request->getVersion(), 1, "input format is invalid", "pids", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        } catch (const std::out_of_range& e) {
            res = createError(ctx->request->getVersion(), 1, "input format is invalid", "pids", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        std::unique_ptr<db::Command> cmd = db::Database::craftGetUserMiiCommand(pid);
        tasks.push_back(std::move(db->runCommand(std::move(cmd))));
    }

    auto results = co_await waitForAll(std::move(tasks));

    pugi::xml_document doc;
    pugi::xml_node miis = doc.append_child("miis");

    int i = 0;
    for (const auto& result : results) {
        if (result.getStatus() != db::DBResultStatus::SUCCESS) {
            res = createError(ctx->request->getVersion(), 1, "Failed to obtain MII data", "", HTTP_STATUS_INTERNAL_SERVER_ERROR);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto miiData = std::move(result.getData<db::DBUserMii>());
        pugi::xml_node miiNode = miis.append_child("mii");
        miiNode.append_child("data").text().set(miiData.miiData.c_str(), miiData.miiData.size());
        miiNode.append_child("id").text().set(std::to_string(miiData.miiId));
        miiNode.append_child("name").text().set(miiData.miiName.c_str(), miiData.miiName.size());
        miiNode.append_child("pid").text().set(input[i].c_str(), input[i].size());
        miiNode.append_child("primary").text().set(miiData.miiPrimary ? "Y" : "N");
        miiNode.append_child("user_id").text().set(miiData.username.c_str(), miiData.username.size());

        const std::array<std::string, 8> image_types = {
            "standard", "normal_face", "frustrated", "smile_open_mouth", "wink_left", "sorrow",
            "surprised_open_mouth", "body"
        };

        pugi::xml_node imagesNode = miiNode.append_child("images");
        for (const auto& imageType : image_types) {
            pugi::xml_node imageNode = imagesNode.append_child("image");
            imageNode.append_child("id").text().set(std::to_string(miiData.miiId));
            imageNode.append_child("type").text().set(imageType.c_str(), imageType.size());

            std::string urlType = imageType == "standard" ? "normal_face" : imageType;

            std::string url = "https://mii-secure.account." + settingsManager->getTopDomain() + "/"
                              + urlType + ".png?id=" + std::to_string(miiData.miiId);
            imageNode.append_child("url").text().set(url.c_str(), url.size());
            imageNode.append_child("cached_url").text().set(url.c_str(), url.size());
        }

        i++;
    }

    res = prepareResponse(ctx->request->getVersion(), doc);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

} // namespace acc