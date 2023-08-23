#include "account.hpp"
#include "../../crypto/tools.hpp"
#include "../../constants.hpp"

#include <unordered_map>

namespace acc {

/*
 * Handler for GET https://account.<domain>/v1/api/admin/time
 * Doesn't actually return anything, but the time is in the response headers.
 * Since it's such a simple request, we won't require a device certificate.
 */
http::Response v1_api_admin_time(const http::Request& req, bool& shouldClose) {
    if (req.getMethod() != http::Method::M_GET) {
        return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
    }

    shouldClose = true;

    return prepareResponse(req.getVersion());
}

/*
 * Handler for GET https://account.<domain>/v1/api/admin/mapped_ids
 * Returns a list of mapped ids for the given input.
 * The input can be either a principal id or a username.
 * Requires a device certificate.
 */
http::Response v1_api_admin_mapped_ids(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                       const http::Request& req, sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                                       const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                       const std::function<void(unsigned int)>& unregisterCloseCall,
                                       const std::shared_ptr<SettingsManager>& settingsManager,
                                       const std::shared_ptr<CertManager>& certManager) {

    if (req.getMethod() != http::Method::M_GET) {
        return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
    }

    http::Response res(req.getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(req, settingsManager, certManager, &res, shouldClose)) {
        return res;
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
        return createError(req.getVersion(), 1, "input format is invalid", "input", shouldClose);
    }

    pugi::xml_document doc;
    pugi::xml_node mapped_ids = doc.append_child("mapped_ids");

    for (const auto& id : input) {
        std::unique_ptr<db::Command> cmd;
        if (inputType == "pid") {
            if (!std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit(c); })) {
                return createError(req.getVersion(), 1, "input format is invalid", "input", shouldClose);
            }

            int pid;
            try {
                pid = std::stoi(id);
            } catch (const std::out_of_range &e) {
                return createError(req.getVersion(), 1, "input format is invalid", "input", shouldClose);
            }

            cmd = db->craftGetUserByPIDCommand(pid);
        } else {
            cmd = db->craftGetUserByUsernameCommand(id);
        }

        uint32_t cmdId = db::Database::runCommand(db, std::move(cmd), registerCloseCall, unregisterCloseCall, shouldStop);

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

    return prepareResponse(req.getVersion(), doc);
}

/*
 * Handler for POST https://account.<domain>/v1/api/oauth20/access_token/generate
 * Generates an access token for the given user.
 * Requires a device certificate. The password can be given directly or as a hash.
 */
http::Response v1_api_access_token_gen(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                       const http::Request& req, sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                                       const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                       const std::function<void(unsigned int)>& unregisterCloseCall,
                                       const std::shared_ptr<SettingsManager>& settingsManager,
                                       const std::shared_ptr<CertManager>& certManager) {

    if (req.getMethod() != http::Method::M_POST) {
        return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
    }

    http::Response res(req.getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(req, settingsManager, certManager, &res, shouldClose)) {
        return res;
    }

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
        uint32_t cmdId = db::Database::runCommand(db, std::move(cmd), registerCloseCall, unregisterCloseCall, shouldStop);

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

        // Verifying a password takes a while, which could allow an attacker to distinguish between
        // valid and invalid usernames. To prevent this, we could always verify a password, even if the
        // username is invalid. But since the mapped_ids method already allows to find valid usernames,
        // we can just return an error if the username is invalid without care for timing attacks.

        // Another possible attack is a DDoS by sending a lot of requests with incorrect passwords.
        // To prevent this, we could rate limit requests, but we'll leave it as is for now.
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

    return prepareResponse(req.getVersion(), doc);
}

/*
 * Handler for GET https://account.<domain>/v1/api/provider/nex_token/@me
 * Creates an access token for the given NEX game server.
 * Requires a device certificate, as well as authentication with an
 * access token generated at /v1/api/oauth20/access_token/generate.
 */
http::Response v1_api_provider_nex_token(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                         const http::Request& req, sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                                         const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                         const std::function<void(unsigned int)>& unregisterCloseCall,
                                         const std::shared_ptr<SettingsManager>& settingsManager,
                                         const std::shared_ptr<CertManager>& certManager) {

    if (req.getMethod() != http::Method::M_GET) {
        return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
    }

    http::Response res(req.getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(req, settingsManager, certManager, &res, shouldClose, false)) {
        return res;
    }

    if (!req.hasQuery("game_server_id")) {
        return createError(req.getVersion(), 3, "Missing game_server_id", "game_server_id", shouldClose);
    }

    std::string gameServerId = req.getQuery("game_server_id");

    if (gameServerId.size() != 8 || !std::all_of(gameServerId.begin(),gameServerId.end(),
                                                                   [](char c) { return std::isxdigit(c); })) {
        return createError(req.getVersion(), 1, "game_server_id is invalid", "game_server_id", shouldClose);
    }

    if (!req.hasHeader("authorization")) {
        return createError(req.getVersion(), 5, "Invalid access token", "access_token", shouldClose);
    }

    std::string token = req.getHeader("authorization")[0];

    if (token.substr(0, 7) != "Bearer ") {
        return createError(req.getVersion(), 5, "Invalid access token", "access_token", shouldClose);
    }

    token = token.substr(7);
    if (!crypto::verifyJWT(settingsManager->getTokenKey(), token)) {
        return createError(req.getVersion(), 5, "Invalid access token", "access_token", shouldClose);
    }

    json tokenPayload = json::parse(crypto::base64UrlDecode(token.substr(token.find('.') + 1,
                                                                         token.rfind('.') - token.find('.') - 1)));

    if (time(nullptr) > tokenPayload["exp"].get<time_t>()) {
        return createError(req.getVersion(), 5, "Invalid access token", "access_token", shouldClose);
    }

    int pid = tokenPayload["sub"].get<int>();
    std::unique_ptr<db::Command> cmd = db->craftGetGameServerAccessCommand(pid, req.getQuery("game_server_id"));
    uint32_t cmdId = db::Database::runCommand(db, std::move(cmd), registerCloseCall, unregisterCloseCall, shouldStop);

    std::unique_ptr<db::Result> results = db->getResult(cmdId);
    if (results->data.empty()) {
        return createError(req.getVersion(), 1016, "NEX account not found", "", shouldClose);
    }

    json jwtPayload = {
            {"exp", time(nullptr) + 3600},
            {"iss", "account"},
            {"sub", pid},
            {"game_server_id", gameServerId}
    };

    std::string tokenJwt = crypto::signJWT(settingsManager->getNEXTokenKey(), jwtPayload);

    auto nexPassword = std::any_cast<std::string>(results->data[0]);
    auto gameServerHost = settingsManager->getGameServerHost(gameServerId);

    pugi::xml_document doc;
    pugi::xml_node nex_token = doc.append_child("nex_token");
    nex_token.append_child("pid").text().set(std::to_string(pid).c_str(), std::to_string(pid).length());
    nex_token.append_child("nex_password").text().set(nexPassword.c_str(), nexPassword.length());
    nex_token.append_child("token").text().set(tokenJwt.c_str(), tokenJwt.length());

    auto gameServerIp = gameServerHost.substr(0, gameServerHost.find(':'));
    auto gameServerPort = gameServerHost.substr(gameServerHost.find(':') + 1);

    nex_token.append_child("host").text().set(gameServerIp.c_str(), gameServerIp.length());
    nex_token.append_child("port").text().set(gameServerPort.c_str(), gameServerPort.length());

    logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                "User with PID " + std::to_string(pid) + " successfully obtained NEX token for game server " + gameServerId);

    shouldClose = true;

    return prepareResponse(req.getVersion(), doc);
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

    return res;
}

http::Response errorHandler(const std::shared_ptr<Logger::Logger>& logger, const http::Request& req, sock::IPv4Addr client,
                            int httpStatus) {
    bool shouldClose = false;
    switch (httpStatus) {
        case HTTP_STATUS_NOT_FOUND:
            return createError(req.getVersion(), 8, "Not Found", "", shouldClose);
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
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
    if (version == http::Version::HTTP_1_1) res.setHeader("Connection", "close");

    return res;
}

http::Response prepareResponse(http::Version version, pugi::xml_document& doc) {
    // Set declaration
    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    decl.append_attribute("standalone") = "yes";

    std::stringstream ss;
    doc.save(ss);

    std::string body = ss.str();
    std::vector<uint8_t> bodyVec(body.begin(), body.end());

    http::Response res = prepareResponse(version);
    res.setHeader("Content-Type", "text/xml");
    res.setBody(bodyVec);

    return res;
}

bool checkDeviceCert(const std::string& cert, EVP_PKEY* pubKey) {
    std::vector<uint8_t> certBin;

    try {
        certBin = crypto::base64Decode(cert);
    } catch (std::runtime_error& e) {
        return false;
    }

    if (certBin.size() != 384) return false;

    std::vector<uint8_t> signature = std::vector<uint8_t>(certBin.begin() + 0x4, certBin.begin() + 0x40);
    std::vector<uint8_t> certData = std::vector<uint8_t>(certBin.begin() + 0x80, certBin.end());

    return crypto::verifyECDSASignature(signature, certData, pubKey);
}

bool checkRequestParams(const http::Request& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<CertManager>& certManager, http::Response* resOut, bool& shouldClose,
                        bool checkDevice) {
    if (checkDevice) {
        if (!req.hasHeader("x-nintendo-device-cert")) {
            *resOut = createError(req.getVersion(), 110, "Unlinked device", "", shouldClose);
            return false;
        }

        std::string deviceCert = req.getHeader("x-nintendo-device-cert")[0];
        EVP_PKEY* realWiiUKey = crypto::loadPublicKey(WII_U_PUB_KEY);
        EVP_PKEY* genWiiUKey = certManager->getDeviceKey();
        if (!(settingsManager->allowRealWiiU() && checkDeviceCert(deviceCert, realWiiUKey))
            && !(settingsManager->allowGeneratedWiiU() && checkDeviceCert(deviceCert, genWiiUKey))) {
            EVP_PKEY_free(realWiiUKey);
            *resOut = createError(req.getVersion(), 1600, "Unable to process request", "Bad Request", shouldClose);
            return false;
        }

        EVP_PKEY_free(realWiiUKey);
    }

    // TODO Maybe check more headers?

    return true;
}

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<CertManager> certMgr, std::shared_ptr<db::Database> db) {

    std::string domain = settingsMgr->getTopDomain();

    server->registerRoute("account." + domain, "/v1/api/admin/time", [](
            const std::shared_ptr<Logger::Logger>&, const http::Request& req, sock::IPv4Addr, bool& shouldStop,
            bool& shouldClose, const std::function<unsigned int(std::function<void()>)>&,
            const std::function<void(unsigned int)>&) -> http::Response { return v1_api_admin_time(req, shouldClose); });

    server->registerRoute("account." + domain, "/v1/api/admin/mapped_ids",
                          [&](const std::shared_ptr<Logger::Logger>& logger, const http::Request& req,
                              sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                              const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                              const std::function<void(unsigned int)>& unregisterCloseCall) -> http::Response {
                              return v1_api_admin_mapped_ids(logger, db, req, client, shouldStop, shouldClose,
                                                             registerCloseCall, unregisterCloseCall, settingsMgr,
                                                             certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/oauth20/access_token/generate",
                          [&](const std::shared_ptr<Logger::Logger>& logger, const http::Request& req,
                              sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                              const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                              const std::function<void(unsigned int)>& unregisterCloseCall) -> http::Response {
                             return v1_api_access_token_gen(logger, db, req, client, shouldStop, shouldClose,
                                                             registerCloseCall, unregisterCloseCall, settingsMgr,
                                                             certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/provider/nex_token/@me",
                          [&](const std::shared_ptr<Logger::Logger>& logger, const http::Request& req,
                              sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                              const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                              const std::function<void(unsigned int)>& unregisterCloseCall) -> http::Response {
                              return v1_api_provider_nex_token(logger, db, req, client, shouldStop, shouldClose,
                                                             registerCloseCall, unregisterCloseCall, settingsMgr,
                                                             certMgr);
                          });

    server->registerErrorPage("account." + domain, errorHandler);
}

} // namespace acc