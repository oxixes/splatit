#include "boss.hpp"

#include <pugixml.hpp>

#include "../../util/util.hpp"

namespace boss {

using namespace async;

/*
 * Handler for GET https://npts.app.<domain>/p01/tasksheet/1/<titleId>/<tasksheetId>
 * Returns the appropiate tasksheet requested, based on the title id (which game)
 * and tasksheet id.
 */
Task<void> p01_tasksheet(http::Server* srv, std::shared_ptr<http::Context> ctx,
                         const std::string& titleId, const std::string& tasksheetId,
                         const std::shared_ptr<SettingsManager>& settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_METHOD_NOT_ALLOWED, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json tasksheetManifest = bossManifest["tasksheets"][titleId]["tasksheets"][tasksheetId];

    pugi::xml_document doc;

    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    pugi::xml_node tasksheet = doc.append_child("TaskSheet");
    tasksheet.append_child("TitleId").text().set(bossManifest["tasksheets"][titleId]["titleId"].get<std::string>().c_str());
    tasksheet.append_child("TaskId").text().set(tasksheetId.c_str());
    tasksheet.append_child("ServiceStatus").text().set(tasksheetManifest["open"] ? "open" : "closed");

    pugi::xml_node files = tasksheet.append_child("Files");
    for (auto& file : tasksheetManifest["files"].items()) {
        fs::path filePath = settingsMgr->getBOSSPath() / file.value()["path"];
        if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
            ctx->logger->log(Logger::level::WARN, Logger::group::BOSS,
                        "The BOSS file " + file.value()["path"].get<std::string>() + " does not exist or is not a file.");

            std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        pugi::xml_node fileNode = files.append_child("File");
        fileNode.append_child("Filename").text().set(file.value()["filename"].get<std::string>().c_str());
        fileNode.append_child("DataId").text().set(file.value()["id"].get<int>());
        fileNode.append_child("Type").text().set(file.value()["type"].get<std::string>().c_str());

        std::string url = "https://npdi.cdn.";
        url.append(settingsMgr->getTopDomain()).append("/p01/data/1/").append(titleId).append("/")
                          .append(std::to_string(file.value()["id"].get<int>())).append("/").append(file.key());

        fileNode.append_child("Url").text().set(url.c_str());

        size_t fileSize = fs::file_size(filePath);
        fileNode.append_child("Size").text().set(fileSize);

        pugi::xml_node notify = fileNode.append_child("Notify");
        notify.append_child("New").text().set(file.value()["notify"]["new"].get<std::string>().c_str());
        notify.append_child("LED").text().set(file.value()["notify"]["LED"].get<bool>());
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    res->setHeader("Content-Type", "application/xml; charset=utf-8");
    res->setHeader("Date", util::getDateHeader());
    res->setHeader("Cache-Control", "private, must-revalidate, max-age=0");
    res->setHeader("X-Frame-Options", "SAMEORIGIN");
    res->setHeader("X-Content-Type-Options", "nosniff, nosniff");
    res->setHeader("X-XSS-Protection", "1; mode=block");
    res->setHeader("X-Permitted-Cross-Domain-Policies", "none");
    res->setHeader("X-Download-Options", "noopen");

    std::filesystem::file_time_type lastModified = fs::last_write_time(settingsMgr->getBOSSPath() / "manifest.json");
    time_t lastModifiedTime = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<
            std::chrono::system_clock::duration>(lastModified - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now()));

    res->setHeader("Last-Modified", util::getDateHeader(lastModifiedTime));
    if (ctx->request->getVersion() == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    std::stringstream ss;
    doc.save(ss, "    ");

    std::string body = ss.str();
    std::vector<uint8_t> bodyVec(body.begin(), body.end());
    res->setBody(std::move(bodyVec));

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://npdi.cdn.<domain>/p01/data/1/<titleId>/<dataId>/<fileHash>
 * Returns the requested file. These URLs are obtained from the tasksheets returned by
 * p01_tasksheet.
 */
Task<void> p01_data(http::Server* srv, std::shared_ptr<http::Context> ctx, const std::string& titleId,
                    const std::string& tasksheetId, const std::string& fileHash,
                    const std::shared_ptr<SettingsManager>& settingsMgr) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_METHOD_NOT_ALLOWED, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    fs::path filePath = settingsMgr->getBOSSPath() / bossManifest["tasksheets"][titleId]["tasksheets"][tasksheetId]["files"][fileHash]["path"];

    if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::ifstream file(filePath, std::ios::binary);
    std::vector<uint8_t> body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    res->setHeader("Content-Type", "applicatoin/octet-stream"); // Yes, "applicatoin", Nintendo made a typo
    res->setHeader("Date", util::getDateHeader());
    res->setHeader("Cache-Control", "private, max-age=33835");
    res->setHeader("X-Frame-Options", "SAMEORIGIN");
    res->setHeader("X-Content-Type-Options", "nosniff, nosniff");
    res->setHeader("X-XSS-Protection", "1; mode=block");
    res->setHeader("X-Permitted-Cross-Domain-Policies", "none");
    res->setHeader("X-Download-Options", "noopen");
    res->setHeader("Referrer-Policy", "strict-origin-when-cross-origin");
    res->setHeader("Content-Disposition", "attachment");
    res->setHeader("Content-Tranfer-Encoding", "binary");

    std::filesystem::file_time_type lastModified = fs::last_write_time(filePath);
    time_t lastModifiedTime = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<
            std::chrono::system_clock::duration>(lastModified - std::filesystem::file_time_type::clock::now() +
                                                 std::chrono::system_clock::now()));

    res->setHeader("Last-Modified", util::getDateHeader(lastModifiedTime));
    if (ctx->request->getVersion() == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    res->setBody(std::move(body));

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://nppl.app.<domain>/p01/policylist/<console type>/<major version>/<country>
 * Returns the requested policy list.
 */
Task<void> p01_policylist(http::Server* srv, std::shared_ptr<http::Context> ctx) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_METHOD_NOT_ALLOWED, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Get the country
    std::string country = ctx->request->getPath().substr(ctx->request->getPath().find_last_of('/') + 1);
    if (country.size() != 2) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (bossManifest["policyLists"].find(country) == bossManifest["policyLists"].end() &&
        bossManifest["policyLists"].find(bossManifest["backupPolicyCountry"]) == bossManifest["policyLists"].end()) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json policyList = bossManifest["policyLists"].find(country) != bossManifest["policyLists"].end() ?
                      bossManifest["policyLists"][country] : bossManifest["policyLists"][bossManifest["backupPolicyCountry"]];

    pugi::xml_document doc;
    pugi::xml_node policyListNode = doc.append_child("PolicyList");
    std::string major = std::to_string(policyList["major"].get<int>());
    policyListNode.append_child("MajorVersion").text().set(major.c_str(), major.size());
    std::string minor = std::to_string(policyList["minor"].get<int>());
    policyListNode.append_child("MinorVersion").text().set(minor.c_str(), minor.size());
    std::string id = std::to_string(policyList["id"].get<int>());
    policyListNode.append_child("ListId").text().set(id.c_str(), id.size());
    std::string defaultStop = (policyList["defaultStop"].get<bool>() ? "true" : "false");
    policyListNode.append_child("DefaultStop").text().set(defaultStop.c_str(), defaultStop.size());
    std::string forceVersionUp = (policyList["forceVersionUp"].get<bool>() ? "true" : "false");
    policyListNode.append_child("ForceVersionUp").text().set(forceVersionUp.c_str(), forceVersionUp.size());
    policyListNode.append_child("UpdateTime").text().set(policyList["updateTime"].get<std::string>().c_str(), policyList["updateTime"].get<std::string>().size());
    for (auto& priority : policyList["titles"].items()) {
        pugi::xml_node priorityNode = policyListNode.append_child("Priority");
        priorityNode.append_child("TitleId").text().set(priority.key().c_str(), priority.key().size());
        priorityNode.append_child("TaskId").text().set(priority.value()["id"].get<std::string>().c_str(), priority.value()["id"].get<std::string>().size());
        priorityNode.append_child("Level").text().set(priority.value()["level"].get<std::string>().c_str(), priority.value()["level"].get<std::string>().size());
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    res->setHeader("Content-Type", "application/xml; charset=utf-8");
    res->setHeader("Date", util::getDateHeader());
    if (ctx->request->getVersion() == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    std::stringstream ss;
    doc.save(ss, "    ");

    std::string body = ss.str();
    std::vector<uint8_t> bodyVec(body.begin(), body.end());
    res->setBody(std::move(bodyVec));

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

std::unique_ptr<http::Response> getError(int status, http::Version version) {
    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(version, status);

    res->setHeader("Content-Type", "text/html; charset=UTF-8");
    res->setHeader("Date", util::getDateHeader());
    if (version == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    std::string error = http::STATUS_CODE_MSG.at(status);
    std::vector<uint8_t> body(error.begin(), error.end());

    res->setBody(body);

    return std::move(res);
}

void registerRoutes(const std::shared_ptr<http::Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr) {
    std::string domain = settingsMgr->getTopDomain();

    for (auto& title : bossManifest["tasksheets"].items()) {
        const std::string& titleId = title.key();
        for (auto& tasksheet : title.value()["tasksheets"].items()) {
            const std::string& tasksheetId = tasksheet.key();
            std::string path = "/p01/tasksheet/1/";
            path.append(titleId).append("/").append(tasksheetId);

            server->registerRoute("npts.app." + domain, path,
                                  [titleId, tasksheetId, settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                return p01_tasksheet(srv, std::move(ctx), titleId, tasksheetId, settingsMgr);
            });

            for (auto& file : tasksheet.value()["files"].items()) {
                const std::string& fileHash = file.key();
                const std::string fileId = std::to_string(file.value()["id"].get<int>());
                path = "/p01/data/1/";
                path.append(titleId).append("/").append(fileId).append("/").append(fileHash);


                server->registerRoute("npdi.cdn." + domain, path,
                                      [titleId, tasksheetId, fileHash, settingsMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                    return p01_data(srv, std::move(ctx), titleId, tasksheetId, fileHash, settingsMgr);
                });
            }
        }
    }

    server->registerRegexRoute("nppl.app." + domain, R"(^\/p01\/policylist\/1\/1\/[A-Z]{2}$)",
                               [](http::Server* srv, std::shared_ptr<http::Context> ctx) {
        return p01_policylist(srv, std::move(ctx));
    });

    std::function errorHandler = [](http::Server* srv, std::shared_ptr<http::Context> ctx) -> Task<void> {
        std::unique_ptr<http::Response> res = getError(ctx->status, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    };

    server->registerErrorPage("npts.app." + domain, errorHandler);
    server->registerErrorPage("npdi.cdn." + domain, errorHandler);
}

void unregisterRoutes(const std::shared_ptr<http::Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr) {
    std::string domain = settingsMgr->getTopDomain();
    server->unregisterHost("npts.app." + domain);
    server->unregisterHost("npdi.cdn." + domain);
    server->unregisterHost("nppl.app." + domain);
}

} // namespace boss