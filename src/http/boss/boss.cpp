#include "boss.hpp"

#include <pugixml.hpp>

#include "../../util/util.hpp"
#include "../../boss/utils.hpp"

namespace boss {

using namespace async;

/*
 * Handler for GET https://npts.app.<domain>/p01/tasksheet/1/<titleId>/<tasksheetId>
 * Returns the appropiate tasksheet requested, based on the title id (which game)
 * and tasksheet id.
 */
Task<void> p01_tasksheet(http::Server* srv, std::shared_ptr<http::Context> ctx,
                         std::shared_ptr<SettingsManager> settingsMgr,
                         std::shared_ptr<db::Database> db) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_METHOD_NOT_ALLOWED, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Extract titleId and tasksheetId from path
    // Path format: /p01/tasksheet/1/<titleId>/<tasksheetId>
    std::string path = ctx->request->getPath();
    size_t pos = path.find("/p01/tasksheet/1/");
    if (pos == std::string::npos) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string remainder = path.substr(pos + 17); // Skip "/p01/tasksheet/1/"
    size_t slashPos = remainder.find('/');
    if (slashPos == std::string::npos) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string titleId = remainder.substr(0, slashPos);
    std::string tasksheetId = remainder.substr(slashPos + 1);

    json bossManifest = co_await getManifest(ctx->logger, db);

    if (!bossManifest["tasksheets"].contains(titleId)) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!bossManifest["tasksheets"][titleId]["tasksheets"].contains(tasksheetId)) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
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
        pugi::xml_node fileNode = files.append_child("File");
        fileNode.append_child("Filename").text().set(file.value()["filename"].get<std::string>().c_str());
        fileNode.append_child("DataId").text().set(file.value()["id"].get<int>());
        fileNode.append_child("Type").text().set(file.value()["type"].get<std::string>().c_str());

        std::string url = "https://npdi.cdn.";
        url.append(settingsMgr->getTopDomain()).append("/p01/data/1/").append(titleId).append("/")
                          .append(std::to_string(file.value()["id"].get<int>())).append("/").append(file.key());

        fileNode.append_child("Url").text().set(url.c_str());
        fileNode.append_child("Size").text().set(file.value()["size"].get<int64_t>());

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

    time_t lastModifiedTime = bossManifest["lastUpdate"].get<time_t>();
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
Task<void> p01_data(http::Server* srv, std::shared_ptr<http::Context> ctx,
                    std::shared_ptr<db::Database> db) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_METHOD_NOT_ALLOWED, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Extract titleId, dataId, and fileHash from path
    // Path format: /p01/data/1/<titleId>/<dataId>/<fileHash>
    std::string path = ctx->request->getPath();
    size_t pos = path.find("/p01/data/1/");
    if (pos == std::string::npos) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string remainder = path.substr(pos + 12); // Skip "/p01/data/1/"
    size_t firstSlash = remainder.find('/');
    if (firstSlash == std::string::npos) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string titleId = remainder.substr(0, firstSlash);
    remainder = remainder.substr(firstSlash + 1);

    size_t secondSlash = remainder.find('/');
    if (secondSlash == std::string::npos) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string dataId = remainder.substr(0, secondSlash);
    std::string fileHash = remainder.substr(secondSlash + 1);

    auto getFileCmd = db::Database::craftGetFileCommand(fileHash);
    db::Result results = co_await db->runCommand(std::move(getFileCmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (!results.hasData()) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto body = results.getData<std::vector<uint8_t>>();

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
    res->setHeader("Last-Modified", util::getDateHeader(std::time(nullptr))); // Maybe this will be better in the future
    if (ctx->request->getVersion() == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    res->setBody(std::move(body));

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://nppl.app.<domain>/p01/policylist/<console type>/<major version>/<country>
 * Returns the requested policy list.
 */
Task<void> p01_policylist(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<db::Database> db) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_METHOD_NOT_ALLOWED, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Get the country
    std::string country = ctx->request->getPath().substr(ctx->request->getPath().find_last_of('/') + 1);
    if (country.size() > 3) {
        std::unique_ptr<http::Response> res = getError(HTTP_STATUS_NOT_FOUND, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    json bossManifest = co_await getManifest(ctx->logger, db);

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

void registerRoutes(const std::shared_ptr<http::Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr,
                    std::shared_ptr<db::Database> db) {
    std::string domain = settingsMgr->getTopDomain();

    server->registerRegexRoute("npts.app." + domain, R"(^\/p01\/tasksheet\/1\/[a-zA-Z0-9]{16}\/[a-zA-Z0-9]{1,16}$)",
                               [settingsMgr, db](http::Server* srv, std::shared_ptr<http::Context> ctx) {
        return p01_tasksheet(srv, std::move(ctx), settingsMgr, db);
    });

    server->registerRegexRoute("npdi.cdn." + domain, R"(^\/p01\/data\/1\/[a-zA-Z0-9]{16}\/[0-9]+\/[0-9a-fA-F]{32}$)",
                               [db](http::Server* srv, std::shared_ptr<http::Context> ctx) {
        return p01_data(srv, std::move(ctx), db);
    });

    server->registerRegexRoute("nppl.app." + domain, R"(^\/p01\/policylist\/1\/1\/[A-Z]{2,3}$)",
                               [db](http::Server* srv, std::shared_ptr<http::Context> ctx) {
        return p01_policylist(srv, std::move(ctx), db);
    });

    std::function errorHandler = [](http::Server* srv, std::shared_ptr<http::Context> ctx) -> Task<void> {
        std::unique_ptr<http::Response> res = getError(ctx->status, ctx->request->getVersion());
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    };

    server->registerErrorPage("npts.app." + domain, errorHandler);
    server->registerErrorPage("npdi.cdn." + domain, errorHandler);
    server->registerErrorPage("nppl.app." + domain, errorHandler);
}

void unregisterRoutes(const std::shared_ptr<http::Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr) {
    std::string domain = settingsMgr->getTopDomain();
    server->unregisterHost("npts.app." + domain);
    server->unregisterHost("npdi.cdn." + domain);
    server->unregisterHost("nppl.app." + domain);
}

} // namespace boss