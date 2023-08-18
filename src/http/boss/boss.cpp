#include "boss.hpp"

#include <pugixml.hpp>

namespace boss {

json bossManifest;

bool init(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsMgr) {
    fs::path bossManifestPath = settingsMgr->getBOSSPath() / "manifest.json";

    if (!fs::exists(bossManifestPath) || !fs::is_regular_file(bossManifestPath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The BOSS manifest file does not exist or is not a file.");
        return false;
    }

    try {
        std::ifstream bossManifestFile(bossManifestPath);
        bossManifest = json::parse(bossManifestFile);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while parsing the BOSS manifest file: " + std::string(e.what()));
        return false;
    }

    fs::path schemaFilePath = fs::path("boss.schema.json");
    if (!fs::exists(schemaFilePath) || !fs::is_regular_file(schemaFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The BOSS manifest schema file does not exist or is not a file, cannot validate BOSS manifest.");
        return false;
    }

    json schema;
    json_validator validator;
    try {
        std::ifstream schemaFile(schemaFilePath);
        schema = json::parse(schemaFile);
        validator.set_root_schema(schema);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while parsing the BOSS manifest schema file: " + std::string(e.what()));
        return false;
    }

    try {
        validator.validate(bossManifest);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The BOSS manifest file is invalid: " + std::string(e.what()));
        return false;
    }

    return true;
}

/*
 * Handler for GET https://npts.app.<domain>/p01/tasksheet/1/<titleId>/<tasksheetId>
 * Returns the appropiate tasksheet requested, based on the title id (which game)
 * and tasksheet id.
 */
http::Response p01_tasksheet(const std::shared_ptr<Logger::Logger>& logger, const http::Request& req,
                             const std::string& titleId, const std::string& tasksheetId,
                             const std::shared_ptr<SettingsManager>& settingsMgr) {
    if (req.getMethod() != http::Method::M_GET) return getError(HTTP_STATUS_METHOD_NOT_ALLOWED, req.getVersion());

    json tasksheetManifest = bossManifest[titleId]["tasksheets"][tasksheetId];

    pugi::xml_document doc;

    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    pugi::xml_node tasksheet = doc.append_child("TaskSheet");
    tasksheet.append_child("TitleId").text().set(bossManifest[titleId]["titleId"].get<std::string>().c_str());
    tasksheet.append_child("TaskId").text().set(tasksheetId.c_str());
    tasksheet.append_child("ServiceStatus").text().set(tasksheetManifest["open"] ? "open" : "closed");

    pugi::xml_node files = tasksheet.append_child("Files");
    for (auto& file : tasksheetManifest["files"].items()) {
        fs::path filePath = settingsMgr->getBOSSPath() / file.value()["path"];
        if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
            logger->log(Logger::level::WARN, Logger::group::BOSS,
                        "The BOSS file " + file.value()["path"].get<std::string>() + " does not exist or is not a file.");

            return getError(HTTP_STATUS_NOT_FOUND, req.getVersion());
        }

        pugi::xml_node fileNode = files.append_child("File");
        fileNode.append_child("Filename").text().set(file.value()["filename"].get<std::string>().c_str());
        fileNode.append_child("DataId").text().set(file.value()["id"].get<int>());
        fileNode.append_child("Type").text().set(file.value()["type"].get<std::string>().c_str());

        std::string url = "https://npdi.cdn.";
        url.append(settingsMgr->getTopDomain()).append("/p01/data/1/").append(titleId).append("/")
                          .append(tasksheetId).append("/").append(file.key());

        fileNode.append_child("Url").text().set(url.c_str());

        size_t fileSize = fs::file_size(filePath);
        fileNode.append_child("Size").text().set(fileSize);

        pugi::xml_node notify = fileNode.append_child("Notify");
        notify.append_child("New").text().set(file.value()["notify"]["new"].get<std::string>().c_str());
        notify.append_child("LED").text().set(file.value()["notify"]["LED"].get<bool>());
    }

    http::Response res(req.getVersion(), HTTP_STATUS_OK);
    res.setHeader("Content-Type", "application/xml; charset=utf-8");
    res.setHeader("Date", util::getDateHeader());
    res.setHeader("Cache-Control", "private, must-revalidate, max-age=0");
    res.setHeader("X-Frame-Options", "SAMEORIGIN");
    res.setHeader("X-Content-Type-Options", "nosniff, nosniff");
    res.setHeader("X-XSS-Protection", "1; mode=block");
    res.setHeader("X-Permitted-Cross-Domain-Policies", "none");
    res.setHeader("X-Download-Options", "noopen");

    std::chrono::time_point lastModified = fs::last_write_time(settingsMgr->getBOSSPath() / "manifest.json");
    time_t lastModifiedTime = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastModified));

    res.setHeader("Last-Modified", util::getDateHeader(lastModifiedTime));
    if (req.getVersion() == http::Version::HTTP_1_1) res.setHeader("Connection", "close");

    std::stringstream ss;
    doc.save(ss, "    ");

    std::string body = ss.str();
    std::vector<uint8_t> bodyVec(body.begin(), body.end());
    res.setBody(bodyVec);

    return res;
}

/*
 * Handler for GET https://npdi.cdn.<domain>/p01/data/1/<titleId>/<tasksheetId>/<fileHash>
 * Returns the requested file. These URLs are obtained from the tasksheets returned by
 * p01_tasksheet.
 */
http::Response p01_data(const http::Request& req, const std::string& titleId, const std::string& tasksheetId,
                        const std::string& fileHash, const std::shared_ptr<SettingsManager>& settingsMgr) {
    if (req.getMethod() != http::Method::M_GET) return getError(HTTP_STATUS_METHOD_NOT_ALLOWED, req.getVersion());

    fs::path filePath = settingsMgr->getBOSSPath() / bossManifest[titleId]["tasksheets"][tasksheetId]["files"][fileHash]["path"];

    if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        return getError(HTTP_STATUS_NOT_FOUND, req.getVersion());
    }

    std::ifstream file(filePath, std::ios::binary);
    std::vector<uint8_t> body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    http::Response res(req.getVersion(), HTTP_STATUS_OK);
    res.setHeader("Content-Type", "applicatoin/octet-stream"); // Yes, "applicatoin", Nintendo made a typo
    res.setHeader("Date", util::getDateHeader());
    res.setHeader("Cache-Control", "private, max-age=33835");
    res.setHeader("X-Frame-Options", "SAMEORIGIN");
    res.setHeader("X-Content-Type-Options", "nosniff, nosniff");
    res.setHeader("X-XSS-Protection", "1; mode=block");
    res.setHeader("X-Permitted-Cross-Domain-Policies", "none");
    res.setHeader("X-Download-Options", "noopen");
    res.setHeader("Referrer-Policy", "strict-origin-when-cross-origin");
    res.setHeader("Content-Disposition", "attachment");
    res.setHeader("Content-Tranfer-Encoding", "binary");

    std::chrono::time_point lastModified = fs::last_write_time(filePath);
    time_t lastModifiedTime = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastModified));

    res.setHeader("Last-Modified", util::getDateHeader(lastModifiedTime));
    if (req.getVersion() == http::Version::HTTP_1_1) res.setHeader("Connection", "close");

    res.setBody(body);

    return res;
}

http::Response getError(int status, http::Version version) {
    http::Response res(version, status);

    res.setHeader("Content-Type", "text/html; charset=UTF-8");
    res.setHeader("Date", util::getDateHeader());
    if (version == http::Version::HTTP_1_1) res.setHeader("Connection", "close");

    std::string error = http::STATUS_CODE_MSG.at(status);
    std::vector<uint8_t> body(error.begin(), error.end());

    res.setBody(body);

    return res;
}

void registerRoutes(const std::shared_ptr<http::Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr) {
    std::string domain = settingsMgr->getTopDomain();

    for (auto& title : bossManifest.items()) {
        const std::string& titleId = title.key();
        for (auto& tasksheet : title.value()["tasksheets"].items()) {
            const std::string& tasksheetId = tasksheet.key();
            std::string path = "/p01/tasksheet/1/";
            path.append(titleId).append("/").append(tasksheetId);

            server->registerRoute("npts.app." + domain, path,
                                  [titleId, tasksheetId, settingsMgr](const std::shared_ptr<Logger::Logger>& logger,
                                                                      const http::Request& req, sock::IPv4Addr client,
                                                                      bool& shouldStop, bool& shouldClose,
                                                                      const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                                                      const std::function<void(unsigned int)>& unregisterCloseCall) {
                shouldClose = true;
                return p01_tasksheet(logger, req, titleId, tasksheetId, settingsMgr);
            });

            for (auto& file : tasksheet.value()["files"].items()) {
                const std::string& fileHash = file.key();
                path = "/p01/data/1/";
                path.append(titleId).append("/").append(tasksheetId).append("/").append(fileHash);

                server->registerRoute("npdi.cdn." + domain, path,
                                      [titleId, tasksheetId, fileHash, settingsMgr](const std::shared_ptr<Logger::Logger>& logger,
                                                                                      const http::Request& req, sock::IPv4Addr client,
                                                                                      bool& shouldStop, bool& shouldClose,
                                                                                      const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                                                                      const std::function<void(unsigned int)>& unregisterCloseCall) {
                    shouldClose = true;
                    return p01_data(req, titleId, tasksheetId, fileHash, settingsMgr);
                });
            }
        }
    }

    std::function errorHandler = [](const std::shared_ptr<Logger::Logger>& logger, const http::Request& req,
                                    sock::IPv4Addr client, int httpStatus) {
        return getError(httpStatus, req.getVersion());
    };

    server->registerErrorPage("npts.app." + domain, errorHandler);
    server->registerErrorPage("npdi.cdn." + domain, errorHandler);
}

} // namespace boss