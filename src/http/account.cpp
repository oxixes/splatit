#include "account.hpp"
#include "../crypto/tools.hpp"

#include <unordered_map>

namespace acc {

http::Response v1_api_admin_time(const http::Request& req, bool& shouldClose) {
    if (req.getMethod() != http::Method::M_GET) {
        return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
    }

    shouldClose = true;

    return std::move(prepareResponse(req.getVersion()));
}

http::Response v1_api_admin_mapped_ids(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                       const http::Request& req, sock::IPv4Dir client, bool& shouldStop, bool& shouldClose,
                                       const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                       const std::function<void(unsigned int)>& unregisterCloseCall) {
    if (req.getMethod() != http::Method::M_GET) {
        return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
    }

    if (!req.hasQuery("input_type") || !req.hasQuery("output_type") || !req.hasQuery("input")) {
        return createError(req.getVersion(), 3, "Request parameters missing", "", shouldClose);
    }

    std::string inputType = req.getQuery("input_type");
    std::string outputType = req.getQuery("output_type");

    if ((inputType != "pid" && inputType != "user_id") || (outputType != "pid" && outputType != "user_id")) {
        return createError(req.getVersion(), 1, "Unable to process request", "Bad Request", shouldClose);
    }

    std::vector<std::string> input = util::split(req.getQuery("input"), ",");
    // We limit the input to 100 entries, as it would be too much to handle anything more than that
    if (input.empty() || input.size() > 100) {
        return createError(req.getVersion(), 1, "Unable to process request", "Bad Request", shouldClose);
    }

    pugi::xml_document doc;
    pugi::xml_node mapped_ids = doc.append_child("mapped_ids");

    for (const auto& id : input) {
        std::unique_ptr<db::Command> cmd;
        if (inputType == "pid") {
            if (!std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit(c); })) {
                return createError(req.getVersion(), 1600, "Unable to process request", "Bad Request", shouldClose);
            }

            int pid;
            try {
                pid = std::stoi(id);
            } catch (const std::out_of_range &e) {
                return createError(req.getVersion(), 1600, "Unable to process request", "Bad Request", shouldClose);
            }

            cmd = db->craftGetUserByPIDCommand(pid);
        } else {
            cmd = db->craftGetUserByUsernameCommand(id);
        }

        int cmdId = db->queueCommand(std::move(cmd), true);
        unsigned int closeCallId = registerCloseCall([cmdId, &db]() { db->notifyCommand(cmdId); });
        db->processQueue();
        db->waitForCommand(cmdId, std::make_shared<bool>(shouldStop));
        if (shouldStop) throw std::runtime_error("Server is stopping");
        unregisterCloseCall(closeCallId);

        std::unique_ptr<db::Result> results = db->getResult(cmdId);
        if (results->status != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

        // No user found
        if (results->data.empty())
            return createError(req.getVersion(), 1600, "Unable to process request", "Bad Request", shouldClose);

        auto userPid = std::to_string(std::any_cast<int>(results->data[0]));
        auto username = std::any_cast<std::string>(results->data[1]);

        pugi::xml_node mapped_id = mapped_ids.append_child("mapped_id");
        if (inputType == "pid")
            mapped_id.append_child("in_id").text().set(userPid.c_str(), userPid.size());
        else
            mapped_id.append_child("in_id").text().set(username.c_str(), username.size());

        if (outputType == "pid")
            mapped_id.append_child("out_id").text().set(userPid.c_str(), userPid.size());
        else
            mapped_id.append_child("out_id").text().set(username.c_str(), username.size());
    }

    shouldClose = true;

    return std::move(prepareResponse(req.getVersion(), doc));
}

http::Response v1_api_access_token_gen(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                       const http::Request& req, sock::IPv4Dir client, bool& shouldStop, bool& shouldClose,
                                       const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                       const std::function<void(unsigned int)>& unregisterCloseCall,
                                       const std::shared_ptr<SettingsManager>& settingsManager) {
    if (req.getMethod() != http::Method::M_POST) {
        return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
    }

    // TODO Verify client certificate

    if (!req.hasHeader("content-type") || req.getHeader("content-type")[0] != "application/x-www-form-urlencoded") {
        return createError(req.getVersion(), 1600, "Unable to process request", "Bad Request", shouldClose);
    }

    std::string tokenJwt;
    std::string refreshJwt;

    std::string body(req.getBody().begin(), req.getBody().end());
    std::unordered_map<std::string, std::string> bodyMap;
    http::parseQuery(body, bodyMap);

    if (bodyMap.find("grant_type") == bodyMap.end() || (bodyMap["grant_type"] != "password" && bodyMap["grant_type"] != "refresh_token")) {
        return createError(req.getVersion(), 4, "Invalid Grant Type", "grant_type", shouldClose);
    }

    if (bodyMap["grant_type"] == "password") {
        if (bodyMap.find("user_id") == bodyMap.end() || bodyMap.find("password") == bodyMap.end()) {
            return createError(req.getVersion(), 3, "Request parameters missing", "", shouldClose);
        }

        std::string userId = bodyMap["user_id"];

        std::unique_ptr<db::Command> cmd = db->craftGetUserByUsernameCommand(userId);
        int cmdId = db->queueCommand(std::move(cmd), true);
        unsigned int closeCallId = registerCloseCall([cmdId, &db]() { db->notifyCommand(cmdId); });
        db->processQueue();
        db->waitForCommand(cmdId, std::make_shared<bool>(shouldStop));
        if (shouldStop) throw std::runtime_error("Server is stopping");
        unregisterCloseCall(closeCallId);

        std::unique_ptr<db::Result> results = db->getResult(cmdId);
        if (results->status != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

        if (results->data.empty()) {
            return createError(req.getVersion(), 106, "Invalid account ID or password", "", shouldClose);
        }

        uint32_t pid = std::any_cast<int>(results->data[0]);

        std::string nintendoPasswordHash;
        if (bodyMap.find("password_type") != bodyMap.end() && bodyMap["password_type"] == "hash") {
            nintendoPasswordHash = bodyMap["password"];
        } else {
            nintendoPasswordHash = crypto::genNintendoPasswordHash(pid, bodyMap["password"]);
        }

        if (!crypto::verifyPassword(nintendoPasswordHash, std::any_cast<std::string>(results->data[2]))) {
            logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " tried to log in with "
                    "invalid password (client " + util::ipv4ToString(client) + ").");
            return createError(req.getVersion(), 106, "Invalid account ID or password", "", shouldClose);
        }

        // At this point, the username is found and the password is correct
        json jwtPayload = {
                {"exp", time(nullptr) + 3600},
                {"iss", "account"},
                {"sub", pid}
        };

        logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " logged in successfully.");

        tokenJwt = crypto::signJWT(settingsManager->getTokenKey(), jwtPayload);
        refreshJwt = crypto::signJWT(settingsManager->getRefreshTokenKey(), jwtPayload);
    } else {
        if (bodyMap.find("refresh_token") == bodyMap.end()) {
            return createError(req.getVersion(), 3, "Missing refresh_token", "refresh_token", shouldClose);
        }

        std::string refreshToken = bodyMap["refresh_token"];

        if (!crypto::verifyJWT(settingsManager->getRefreshTokenKey(), refreshToken)) {
            return createError(req.getVersion(), 5, "Invalid access token", "access_token", shouldClose);
        }

        // We assume the structure is valid since we just verified its signature
        json refreshTokenPayload = json::parse(crypto::base64UrlDecode(refreshToken.substr(refreshToken.find('.') + 1,
                                                                   refreshToken.rfind('.') - refreshToken.find('.') - 1)));

        if (time(nullptr) > refreshTokenPayload["exp"].get<time_t>()) {
            return createError(req.getVersion(), 5, "Invalid access token", "access_token", shouldClose);
        }

        json jwtPayload = {
                {"exp", time(nullptr) + 3600},
                {"iss", "account"},
                {"sub", refreshTokenPayload["sub"]}
        };

        logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                    "User with PID " + std::to_string(refreshTokenPayload["sub"].get<uint32_t>()) + " refreshed their access token.");

        tokenJwt = crypto::signJWT(settingsManager->getTokenKey(), jwtPayload);
        refreshJwt = crypto::signJWT(settingsManager->getRefreshTokenKey(), jwtPayload);
    }

    pugi::xml_document doc;

    pugi::xml_node oauth20 = doc.append_child("OAuth20");
    pugi::xml_node access_token = oauth20.append_child("access_token");
    access_token.append_child("token").text().set(tokenJwt.c_str(), tokenJwt.length());
    access_token.append_child("refresh_token").text().set(refreshJwt.c_str(), refreshJwt.length());
    access_token.append_child("expires_in").text().set("3600");

    shouldClose = true;

    return std::move(prepareResponse(req.getVersion(), doc));
}

http::Response createError(http::Version version, int code, const std::string& message, const std::string& cause,
                           bool& shouldClose) {
    pugi::xml_document doc;

    pugi::xml_node errors = doc.append_child("errors");
    pugi::xml_node error = errors.append_child("error");

    std::string codeStr;
    // Pad with 0s
    for (int i = 0; i < 4 - std::to_string(code).length(); i++) {
        codeStr += "0";
    }
    codeStr += std::to_string(code);

    error.append_child("cause").text().set(cause.c_str(), cause.size());
    error.append_child("code").text().set(codeStr.c_str(), codeStr.size());
    error.append_child("message").text().set(message.c_str(), message.size());

    http::Response res = prepareResponse(version, doc);

    shouldClose = true;

    return std::move(res);
}

http::Response errorHandler(const std::shared_ptr<Logger::Logger>& logger, const http::Request& req, sock::IPv4Dir client,
                            int httpStatus) {
    bool shouldClose = false;
    switch (httpStatus) {
        case HTTP_STATUS_NOT_FOUND:
            return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
        default:
            return createError(req.getVersion(), 2001, "Unable to process request",
                               "Internal Server Error", shouldClose);
    }
}

http::Response prepareResponse(http::Version version) {
    http::Response res(version, HTTP_STATUS_OK);
    res.setHeader("X-Nintendo-Date", util::getXNintendoDateHeader());
    res.setHeader("Date", util::getDateHeader());
    res.setHeader("Server", "Nintendo 3DS (http)");
    res.setHeader("Connection", "close");

    return std::move(res);
}

http::Response prepareResponse(http::Version version, pugi::xml_document& doc) {
    // Set declaration
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    decl.append_attribute("standalone") = "yes";

    std::stringstream ss;
    doc.save(ss);

    std::string body = ss.str();
    std::vector<unsigned char> bodyVec(body.begin(), body.end());

    http::Response res = prepareResponse(version);
    res.setHeader("Content-Type", "text/xml");
    res.setBody(bodyVec);

    return std::move(res);
}

void registerCalls(const std::shared_ptr<HTTP_Server>& server, const std::string& domain,
                   std::shared_ptr<SettingsManager> settingsMgr) {

    server->registerRoute("account." + domain, "/v1/api/admin/time", [](
            const std::shared_ptr<Logger::Logger>&, const std::shared_ptr<db::Database>&,
            const http::Request& req, sock::IPv4Dir, bool& shouldStop, bool& shouldClose,
            const std::function<unsigned int(std::function<void()>)>&, const std::function<void(unsigned int)>&)
            -> http::Response { return v1_api_admin_time(req, shouldClose); });

    server->registerRoute("account." + domain, "/v1/api/admin/mapped_ids", v1_api_admin_mapped_ids);
    server->registerRoute("account." + domain, "/v1/api/oauth20/access_token/generate",
                          [&](const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                             const http::Request& req, sock::IPv4Dir client, bool& shouldStop, bool& shouldClose,
                             const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                             const std::function<void(unsigned int)>& unregisterCloseCall) -> http::Response {
                              return v1_api_access_token_gen(logger, db, req, client, shouldStop, shouldClose,
                                                             registerCloseCall, unregisterCloseCall, settingsMgr);
                          });

    server->registerErrorPage("account." + domain, errorHandler);
}

} // namespace acc