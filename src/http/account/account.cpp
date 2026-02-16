#include "account.hpp"
#include "../../crypto/tools.hpp"
#include "../../constants.hpp"

#include <unordered_map>
#include <regex>
#include <auth.grpc.pb.h>
#include <date/tz.h>
#include <mailio/smtp.hpp>

#include "../../exceptions.hpp"

// TODO Replace use of time_t with date EVERYWHERE. I write this here because it's the first time I use date in the project.

namespace acc {

using namespace async;
using json = nlohmann::json;

std::shared_ptr<grpcimpl::ChannelPool> channelPool = nullptr;
std::map<std::string, std::vector<std::pair<std::string, std::string>>> gameServerHosts;
std::map<std::string, size_t> gameServerHostIndexRoundRobin;

json timezones;
json regions;
json countriesAndLanguages;
std::string accountSettingsHTML;

/*
 * Handler for POST https://account.<domain>/v1/api/oauth20/access_token/generate
 * Generates an access token for the given user.
 * Requires a device certificate. The password can be given directly or as a hash.
 */
Task<void> v1_api_access_token_gen(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                  std::shared_ptr<db::Database> db,
                                  std::shared_ptr<SettingsManager> settingsManager,
                                  std::shared_ptr<crypto::CertManager> certManager) {

    if (ctx->request->getMethod() != http::Method::M_POST) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string deviceCert = ctx->request->getHeader("x-nintendo-device-cert")[0];
    std::vector<uint8_t> certBin = crypto::base64Decode(deviceCert);
    std::string deviceIdHex(certBin.begin() + 0xC6, certBin.begin() + 0xCE);
    // We convert the hex string to a number
    uint32_t deviceId = std::stoul(deviceIdHex, nullptr, 16);

    if (!ctx->request->hasHeader("content-type") || ctx->request->getHeader("content-type")[0] != "application/x-www-form-urlencoded") {
        res = createError(ctx->request->getVersion(), 1600, "Unable to process request", "Bad Request", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unordered_map<std::string, std::string> bodyMap;
    try {
        http::parseQuery(body, bodyMap);
    } catch (const std::exception&) {
        res = createError(ctx->request->getVersion(), 2, "Malformed request", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!bodyMap.contains("grant_type") || (bodyMap["grant_type"] != "password" && bodyMap["grant_type"] != "refresh_token")) {
        res = createError(ctx->request->getVersion(), 4, "Invalid Grant Type", "grant_type", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (bodyMap["grant_type"] == "password") {
        if (!bodyMap.contains("user_id") || !bodyMap.contains("password")) {
            res = createError(ctx->request->getVersion(), 3, "Request parameters missing", "", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        std::string userId = bodyMap["user_id"];

        std::unique_ptr<db::Command> cmd = db::Database::craftGetUserByUsernameCommand(userId);
        db::Result results = co_await db->runCommand(std::move(cmd));

        if (results.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

        if (!results.hasData()) {
            res = createError(ctx->request->getVersion(), 106, "Invalid account ID or password", "", HTTP_STATUS_FORBIDDEN);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto userData = std::move(results.getData<db::DBUserData>());

        std::string nintendoPasswordHash;
        if (bodyMap.contains("password_type") && bodyMap["password_type"] == "hash") {
            nintendoPasswordHash = bodyMap["password"];
        } else {
            nintendoPasswordHash = crypto::genNintendoPasswordHash(userData.pid, bodyMap["password"]);
        }

        // Verifying a password takes a while, which could allow an attacker to distinguish between
        // valid and invalid usernames. To prevent this, we could always verify a password, even if the
        // username is invalid. But since the mapped_ids method already allows to find valid usernames,
        // we can just return an error if the username is invalid without care for timing attacks.

        // Another possible attack is a DDoS by sending a lot of requests with incorrect passwords.
        // To prevent this, we could rate limit requests, but we'll leave it as is for now.
        if (!crypto::verifyPassword(nintendoPasswordHash, userData.password)) {
            ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " tried to log in with "
                                                                                             "invalid password (client " + util::ipv4ToString(ctx->client) + ").");
            res = createError(ctx->request->getVersion(), 106, "Invalid account ID or password", "", HTTP_STATUS_FORBIDDEN);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto ownershipCmd = db::Database::craftGetOwnershipCommand(userData.pid, deviceId);
        const db::Result ownershipResults = co_await db->runCommand(std::move(ownershipCmd));
        if (ownershipResults.getStatus() != db::DBResultStatus::SUCCESS) {
            throw std::runtime_error("Database error");
        }

        if (!ownershipResults.hasData() || ownershipResults.getData<db::DBOwnershipData>().status != "ACTIVE") {
            ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " tried to log in with "
                             "unowned device (client " + util::ipv4ToString(ctx->client) + ", deviceId " + std::to_string(deviceId) + ").");
            res = createError(ctx->request->getVersion(), 104, "Device is not linked to this account", "", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        // At this point, the username is found and the password is correct
        crypto::AccountToken token {
            .pid = userData.pid,
            .deviceId = deviceId,
            .expiration = static_cast<uint64_t>(time(nullptr)) + 3600,
            .key = crypto::base64Decode(settingsManager->getTokenKey())
        };

        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userId + " logged in successfully.");

        std::string accountToken = crypto::generateAccountToken(token);

        token.key = crypto::base64Decode(settingsManager->getRefreshTokenKey());
        token.expiration = static_cast<uint64_t>(time(nullptr)) + 12 * 3600; // 12 hours for refresh token
        std::string refreshToken = crypto::generateAccountToken(token);

        pugi::xml_document doc;

        pugi::xml_node oauth20 = doc.append_child("OAuth20");
        pugi::xml_node access_token = oauth20.append_child("access_token");
        access_token.append_child("token").text().set(accountToken.c_str(), accountToken.length());
        access_token.append_child("refresh_token").text().set(refreshToken.c_str(), refreshToken.length());
        access_token.append_child("expires_in").text().set("3600");

        res = prepareResponse(ctx->request->getVersion(), doc);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    } else {
        if (!bodyMap.contains("refresh_token")) {
            res = createError(ctx->request->getVersion(), 3, "Missing refresh_token", "refresh_token", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        std::string refreshTokenStr = bodyMap["refresh_token"];
        crypto::AccountToken refreshToken {
            .key = crypto::base64Decode(settingsManager->getRefreshTokenKey())
        };

        if (!crypto::parseAccountToken(refreshTokenStr, refreshToken)) {
            res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        if (time(nullptr) > refreshToken.expiration) {
            res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        crypto::AccountToken token {
            .pid = refreshToken.pid,
            .deviceId = refreshToken.deviceId,
            .expiration = static_cast<uint64_t>(time(nullptr)) + 3600,
            .key = crypto::base64Decode(settingsManager->getTokenKey())
        };

        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                    "User with PID " + std::to_string(refreshToken.pid) + " refreshed their access token.");

        std::string accountToken = crypto::generateAccountToken(token);

        token.key = crypto::base64Decode(settingsManager->getRefreshTokenKey());
        token.expiration = static_cast<uint64_t>(time(nullptr)) + 12 * 3600; // 12 hours for refresh token
        std::string refresh = crypto::generateAccountToken(token);

        pugi::xml_document doc;

        pugi::xml_node oauth20 = doc.append_child("OAuth20");
        pugi::xml_node access_token = oauth20.append_child("access_token");
        access_token.append_child("token").text().set(accountToken.c_str(), accountToken.length());
        access_token.append_child("refresh_token").text().set(refresh.c_str(), refresh.length());
        access_token.append_child("expires_in").text().set("3600");

        res = prepareResponse(ctx->request->getVersion(), doc);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }
}

/*
 * Handler for GET https://mii-secure.account.<domain>/<type>.<format>?id=<mii id>
 * Obtains the image of the Mii with the given id.
 */
Task<void> mii_image(http::Server* srv, std::shared_ptr<http::Context> ctx,
                     std::shared_ptr<db::Database> db,
                     std::shared_ptr<SettingsManager> settingsManager,
                     std::shared_ptr<crypto::CertManager> certManager) {

    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string type =ctx->request->getPath();
    // Split the path into the type and format
    type = type.substr(1, type.find('.') - 1);
    std::string format = ctx->request->getPath().substr(type.length() + 2);

    fs::path miiImagesPath = settingsManager->getMiiImagesPath();
    if (!fs::exists(miiImagesPath)) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Mii images path does not exist.");
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!ctx->request->hasQuery("id")) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    fs::path miiImagePath = miiImagesPath / (ctx->request->getQuery("id") + "_" + type + "." + format);

    // For security purposes, we don't want to expose the file system structure. We make sure the resulting path is inside
    // the mii images directory.
    auto absoluteImagesPath = fs::absolute(miiImagesPath);
    auto absoluteImagePath = fs::absolute(miiImagePath);
    if (absoluteImagePath.string().rfind(absoluteImagesPath.string(), 0)) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!fs::exists(miiImagePath) || !fs::is_regular_file(miiImagePath)) {
        miiImagePath = "miiDefault." + format; // Default image if the requested one does not exist

        if (!fs::exists(miiImagePath) || !fs::is_regular_file(miiImagePath)) {
            ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT, "Mii image not found: " + miiImagePath.string());
            std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT, "Using default Mii image: " + miiImagePath.string());
    }

    std::ifstream file(miiImagePath, std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
    file.close();

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    res->setHeader("Content-Type", "application/octet-stream");
    res->setHeader("Date", util::getDateHeader());
    // Get last modified time
    std::filesystem::file_time_type lastModified = fs::last_write_time(miiImagePath);
    time_t lastModifiedTime = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<
            std::chrono::system_clock::duration>(lastModified - std::filesystem::file_time_type::clock::now() +
                                                 std::chrono::system_clock::now()));

    res->setHeader("Last-Modified", util::getDateHeader(lastModifiedTime));
    res->setHeader("Connection", "close");

    res->setBody(image);

    srv->sendResponse(std::move(ctx), std::move(res), false);
}

std::unique_ptr<http::Response> createError(http::Version version, int code, const std::string& message, const std::string& cause, const int httpStatus) {
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

    return prepareResponse(version, doc, httpStatus);
}

Task<void> errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx) {
    std::unique_ptr<http::Response> res;
    switch (ctx->status) {
        case HTTP_STATUS_NOT_FOUND:
            res = std::move(createError(ctx->request->getVersion(), 8, "Not Found", "", ctx->status));
            break;
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        default:
            res = std::move(createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error", ctx->status));
            break;
    }

    srv->sendResponse(std::move(ctx), std::move(res), false);
    co_return;
}

std::unique_ptr<http::Response> prepareResponse(http::Version version, int httpStatus) {
    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(version, httpStatus);
    res->setHeader("X-Nintendo-Date", util::getXNintendoDateHeader());
    res->setHeader("Date", util::getDateHeader());
    res->setHeader("Server", "Nintendo 3DS (http)");
    if (version == http::Version::HTTP_1_1) res->setHeader("Connection", "close");

    return res;
}

std::unique_ptr<http::Response> prepareResponse(http::Version version, pugi::xml_document& doc, const int httpStatus) {
    // Set declaration
    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    decl.append_attribute("standalone") = "yes";

    std::stringstream ss;
    doc.save(ss);

    std::string body = ss.str();
    // Replace " />" with "/>". The console does not like the space
    const std::string from = " />";
    const std::string to = "/>";
    size_t pos = 0;
    while ((pos = body.find(from, pos)) != std::string::npos) {
        body.replace(pos, from.length(), to);
        pos += to.length();
    }

    std::vector<uint8_t> bodyVec(body.begin(), body.end());

    std::unique_ptr<http::Response> res = prepareResponse(version, httpStatus);
    res->setHeader("Content-Type", "application/xml;charset=UTF-8");
    res->setBody(std::move(bodyVec));

    return res;
}

bool checkDeviceCert(const std::string& cert, EVP_PKEY* pubKey, std::string& deviceId) {
    std::vector<uint8_t> certBin;

    try {
        certBin = crypto::base64Decode(cert);
    } catch (std::runtime_error&) {
        return false;
    }

    if (certBin.size() != 384) return false;

    const std::vector<uint8_t> signature = std::vector(certBin.begin() + 0x4, certBin.begin() + 0x40);
    const std::vector<uint8_t> certData = std::vector(certBin.begin() + 0x80, certBin.end());

    bool verified = crypto::verifyECDSASignature(signature, certData, pubKey);

    if (verified) {
        std::string deviceIdHex(certBin.begin() + 0xC6, certBin.begin() + 0xCE);
        deviceId = std::to_string(std::stoul(deviceIdHex, nullptr, 16));
    }

    return verified;
}

Task<bool> checkOauthToken(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                           const std::shared_ptr<db::Database>& db, crypto::AccountToken& token) {
    if (!req->hasHeader("authorization")) {
        co_return false;
    }

    std::string tokenStr = req->getHeader("authorization")[0];

    if (tokenStr.substr(0, 7) != "Bearer ") {
        co_return false;
    }

    tokenStr = tokenStr.substr(7);
    token.key = crypto::base64Decode(settingsManager->getTokenKey());

    if (!crypto::parseAccountToken(tokenStr, token) || time(nullptr) > token.expiration) {
        co_return false;
    }

    auto cmd = db::Database::craftGetOwnershipCommand(token.pid, token.deviceId);
    const db::Result res = co_await db->runCommand(std::move(cmd));
    if (res.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (!res.hasData() || res.getData<db::DBOwnershipData>().status != "ACTIVE") {
        co_return false; // Database error or ownership not found
    }

    co_return true;
}

Task<std::optional<uint32_t>> checkHashedBasicAuth(std::shared_ptr<db::Database> db,
                                                   std::shared_ptr<http::Context> ctx) {
    if (!ctx->request->hasHeader("authorization")) {
        co_return std::nullopt;
    }

    std::string authHeader = ctx->request->getHeader("authorization")[0];
    if (authHeader.substr(0, 6) != "Basic ") {
        co_return std::nullopt;
    }

    std::string credentials = authHeader.substr(6);
    std::vector<uint8_t> decodedCredentials;
    try {
        decodedCredentials = crypto::base64Decode(credentials);
    } catch (const std::runtime_error&) {
        co_return std::nullopt;
    }
    std::string credentialsStr(decodedCredentials.begin(), decodedCredentials.end());

    const size_t spacePos = credentialsStr.find(' ');
    if (spacePos == std::string::npos) {
        co_return std::nullopt; // Invalid format, no space found
    }

    std::string username = credentialsStr.substr(0, spacePos);
    std::string password = credentialsStr.substr(spacePos + 1);

    if (username.empty() || password.empty()) {
        co_return std::nullopt;
    }

    // Get the user by username
    auto cmd = db::Database::craftGetUserByUsernameCommand(username);
    db::Result res = co_await db->runCommand(std::move(cmd));

    if (res.getStatus() != db::DBResultStatus::SUCCESS || !res.hasData()) {
        co_return std::nullopt; // Database error or user not found
    }

    auto userData = std::move(res.getData<db::DBUserData>());

    auto getProfileCmd = db::Database::craftGetUserProfileCommand(userData.pid);
    db::Result profileRes = co_await db->runCommand(std::move(getProfileCmd));
    if (profileRes.getStatus() != db::DBResultStatus::SUCCESS || !profileRes.hasData()
        || !profileRes.getData<db::DBUserProfileData>().active) {
        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userData.username + " tried to log in but is banned "
                                                                          "(client " + util::ipv4ToString(ctx->client) + ").");
        throw UserBanned("User is banned");
    }

    std::string nintendoPasswordHash = crypto::genNintendoPasswordHash(userData.pid, password);
    if (!crypto::verifyPassword(nintendoPasswordHash, userData.password)) {
        ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userData.username + " tried to log in with "
                                                                          "invalid password (client " + util::ipv4ToString(ctx->client) + ").");
        co_return std::nullopt; // Invalid password, resolve the promise without any data
    }

    ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "User " + userData.username + " logged in successfully.");

    co_return userData.pid; // Valid credentials, return the user ID
}

Task<bool> checkDeviceBanned(uint32_t deviceId, const std::shared_ptr<db::Database>& db) {
    auto getDeviceCmd = db::Database::craftGetDeviceCommand(deviceId);

    db::Result deviceRes = co_await db->runCommand(std::move(getDeviceCmd));
    if (deviceRes.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (deviceRes.hasData() && deviceRes.getData<db::DBDeviceData>().banned) {
        co_return true;
    }

    co_return false;
}

bool checkRequestParams(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<crypto::CertManager>& certManager, std::unique_ptr<http::Response>& resOut,
                        bool checkDevice) {
    if (!req->hasHeader("x-nintendo-device-id")) {
        resOut = createError(req->getVersion(), 2, "X-Nintendo-Device-ID is invalid", "X-Nintendo-Device-ID", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    if (checkDevice) {
        if (!req->hasHeader("x-nintendo-device-cert")) {
            resOut = createError(req->getVersion(), 110, "Unlinked device", "", HTTP_STATUS_FORBIDDEN);
            return false;
        }

        std::string deviceId = req->getHeader("x-nintendo-device-id")[0];
        std::string certDeviceId;

        std::string deviceCert = req->getHeader("x-nintendo-device-cert")[0];
        EVP_PKEY* realWiiUKey = crypto::loadPublicKey(WII_U_PUB_KEY);
        EVP_PKEY* genWiiUKey = certManager->getDeviceKey();
        if (!(settingsManager->allowRealWiiU() && checkDeviceCert(deviceCert, realWiiUKey, certDeviceId))
            && !(settingsManager->allowGeneratedWiiU() && checkDeviceCert(deviceCert, genWiiUKey, certDeviceId))) {
            EVP_PKEY_free(realWiiUKey);
            resOut = createError(req->getVersion(), 1600, "Unable to process request", "Bad Request", HTTP_STATUS_BAD_REQUEST);
            return false;
        }

        EVP_PKEY_free(realWiiUKey);

        if (certDeviceId != deviceId) {
            resOut = createError(req->getVersion(), 2, "X-Nintendo-Device-ID is invalid", "X-Nintendo-Device-ID", HTTP_STATUS_BAD_REQUEST);
            return false;
        }
    } else {
        // Check that device id is a valid number
        try {
            uint32_t id = std::stoul(req->getHeader("x-nintendo-device-id")[0]);
        } catch (const std::invalid_argument&) {
            resOut = createError(req->getVersion(), 2, "X-Nintendo-Device-ID is invalid", "X-Nintendo-Device-ID", HTTP_STATUS_BAD_REQUEST);
            return false;
        } catch (const std::out_of_range&) {
            resOut = createError(req->getVersion(), 2, "X-Nintendo-Device-ID is invalid", "X-Nintendo-Device-ID", HTTP_STATUS_BAD_REQUEST);
            return false;
        }
    }

    if (!req->hasHeader("x-nintendo-country") || !timezones.contains(req->getHeader("x-nintendo-country")[0])) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Country", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    if (!req->hasHeader("x-nintendo-region")) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Region", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    try {
        uint32_t region = std::stoul(req->getHeader("x-nintendo-region")[0]);

        if (region != 1 && region != 2 && region != 4 && region != 8 && region != 16 && region != 32 && region != 64) {
            resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Region", HTTP_STATUS_BAD_REQUEST);
            return false;
        }
    } catch (const std::invalid_argument&) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Region", HTTP_STATUS_BAD_REQUEST);
        return false;
    } catch (const std::out_of_range&) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Region", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    if (!req->hasHeader("x-nintendo-serial-number")) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Serial-Number", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    const std::string serialNumber = req->getHeader("x-nintendo-serial-number")[0];

    // Check serial number format
    std::regex serialRegex("^[FG][EJW]([FHM])?\\d{9}$");

    if (!std::regex_match(serialNumber, serialRegex)) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Serial-Number", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    if (!req->hasHeader("x-nintendo-system-version")) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-System-Version", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    if (req->getHeader("x-nintendo-system-version")[0].length() != 4 || !std::ranges::all_of(req->getHeader("x-nintendo-system-version")[0], ::isdigit)) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-System-Version", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    if (!req->hasHeader("x-nintendo-country")) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Country", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    if (!timezones.contains(req->getHeader("x-nintendo-country")[0])) {
        resOut = createError(req->getVersion(), 2, "Bad request header", "X-Nintendo-Country", HTTP_STATUS_BAD_REQUEST);
        return false;
    }

    return true;
}

bool checkEmailAddress(const std::string& address) {
    // Check that address is a valid email
    if (address.empty() || address.find('@') == std::string::npos) {
        return false;
    }

    // Check that address is not too long
    if (address.length() > 256) {
        return false;
    }

    std::regex emailRegex(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    if (!std::regex_match(address, emailRegex)) {
        return false; // Invalid email format
    }

    // TODO Maybe in the future check connecting to the domain

    return true;
}

bool sendEmail(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsManager,
               mailio::message& msg) {
    if (!settingsManager->isAccountsEmailEnabled()) {
        return true; // Email sending is disabled, so we don't send anything
    }

    std::string smtpServer = settingsManager->getAccountsEmailSettings()["host"].get<std::string>();
    unsigned short smtpPort = settingsManager->getAccountsEmailSettings()["port"].get<unsigned short>();
    std::string smtpUser = settingsManager->getAccountsEmailSettings()["username"].get<std::string>();
    std::string smtpPassword = settingsManager->getAccountsEmailSettings()["password"].get<std::string>();
    std::string smtpFrom = settingsManager->getAccountsEmailSettings()["sender"].get<std::string>();
    uint64_t sendTimeout = settingsManager->getAccountsEmailSettings()["sendTimeout"].get<uint64_t>();

    msg.from(mailio::mail_address("SplatIt", smtpFrom));

    try {
        if (settingsManager->getAccountsEmailSettings()["secure"].get<bool>()) {
            auto authMethod = mailio::smtps::auth_method_t::START_TLS;
            if (settingsManager->getAccountsEmailSettings()["authMethod"] == "none") {
                authMethod = mailio::smtps::auth_method_t::NONE;
            } else if (settingsManager->getAccountsEmailSettings()["authMethod"] == "login") {
                authMethod = mailio::smtps::auth_method_t::LOGIN;
            }

            mailio::smtps conn(smtpServer, smtpPort, std::chrono::milliseconds(sendTimeout));
            conn.authenticate(smtpUser, smtpPassword, authMethod);
            conn.submit(msg);
        } else {
            auto authMethod = mailio::smtp::auth_method_t::LOGIN;
            if (settingsManager->getAccountsEmailSettings()["authMethod"] == "none") {
                authMethod = mailio::smtp::auth_method_t::NONE;
            }

            mailio::smtp conn(smtpServer, smtpPort, std::chrono::milliseconds(sendTimeout));
            conn.authenticate(smtpUser, smtpPassword, authMethod);
            conn.submit(msg);
        }
    } catch (const mailio::smtp_error& e) {
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                    "Failed to send email: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                    "An error occurred while sending email: " + std::string(e.what()));
        return false;
    }

    return true;
}

bool init(const std::shared_ptr<Logger::Logger>& logger) {
    fs::path timezonesFilePath = fs::path("timezones.json");
    if (!fs::exists(timezonesFilePath) || !fs::is_regular_file(timezonesFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The timezones file does not exist or is not a file, cannot initialize the account server.");
        return false;
    }

    try {
        std::ifstream timezonesFile(timezonesFilePath);
        timezones = json::parse(timezonesFile);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to parse timezones file: " + std::string(e.what()));
        return false;
    }

    fs::path accountSettingsHTMLPath = fs::path("account_settings.html");
    if (!fs::exists(accountSettingsHTMLPath) || !fs::is_regular_file(accountSettingsHTMLPath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The account settings HTML file does not exist or is not a file, cannot initialize the account server.");
        return false;
    }

    try {
        std::ifstream accountSettingsHTMLFile(accountSettingsHTMLPath);
        accountSettingsHTML = std::string(std::istreambuf_iterator<char>(accountSettingsHTMLFile), {});
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to parse account settings HTML file: " + std::string(e.what()));
        return false;
    }

    fs::path regionsFilePath = fs::path("regions.json");
    if (!fs::exists(regionsFilePath) || !fs::is_regular_file(regionsFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The regions file does not exist or is not a file, cannot initialize the account server.");
        return false;
    }

    try {
        std::ifstream regionsFile(regionsFilePath);
        regions = json::parse(regionsFile);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to parse regions file: " + std::string(e.what()));
        return false;
    }

    fs::path countriesAndLanguagesFilePath = fs::path("countries_languages.json");
    if (!fs::exists(countriesAndLanguagesFilePath) || !fs::is_regular_file(countriesAndLanguagesFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The country codes file does not exist or is not a file, cannot initialize the account server.");
        return false;
    }

    try {
        std::ifstream countriesAndLanguagesFile(countriesAndLanguagesFilePath);
        countriesAndLanguages = json::parse(countriesAndLanguagesFile);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Failed to parse countries and languages file: " + std::string(e.what()));
    }

    return true;
}

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<crypto::CertManager> certMgr, std::shared_ptr<db::Database> db) {

    channelPool = std::make_shared<grpcimpl::ChannelPool>(settingsMgr->getAccountsgRPCConnectionPoolMaxSize());
    gameServerHosts = std::move(settingsMgr->getGameServerHosts());
    for (const auto& host : gameServerHosts) {
        gameServerHostIndexRoundRobin[host.first] = 0;
    }

    std::string domain = settingsMgr->getTopDomain();

    server->registerRoute("account." + domain, "/v1/api/admin/time", v1_api_admin_time);

    server->registerRoute("account." + domain, "/v1/api/admin/mapped_ids",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                return v1_api_admin_mapped_ids(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/oauth20/access_token/generate",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                return v1_api_access_token_gen(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/provider/nex_token/@me",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_provider_nex_token(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/devices/@current/status",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_devices_current_status(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/devices/@current/inactivate",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_devices_current_inactivate(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/profile",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me_profile(srv, std::move(ctx), db, settingsMgr, certMgr, std::nullopt);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/devices/owner",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me_devices_owner(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/provider/service_token/@me",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_provider_service_token_me(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/emails",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me_emails(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/miis/@primary",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me_miis_primary(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/devices/@current/attributes",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me_devices_current_attributes(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/agreements",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me_agreements(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/devices",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              if (ctx->request->getMethod() == http::Method::M_GET) {
                                  return v1_api_people_me_devices_get(srv, std::move(ctx), db, settingsMgr, certMgr);
                              }

                              return v1_api_people_me_devices_post(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/people/@me/devices/@current/inactivate",
                              [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                  return v1_api_people_me_devices_current_inactivate(srv, std::move(ctx), db, settingsMgr, certMgr);
                              });

    server->registerRoute("account." + domain, "/v1/api/people/@me/deletion",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_people_me_deletion(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/content/agreements/([A-Za-z\-]+)/([A-Z]{2})/(\d{4}|@latest)$)",
                               [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   // Extract the type, country, and version from the request path
                                   const std::regex re(R"(^/v1/api/content/agreements/([A-Za-z\-]+)/([A-Z]{2})/(\d{4}|@latest)$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   std::string type = match[1];
                                   const std::string country = match[2];
                                   const std::string version = match[3];

                                   // Turn type uppercase
                                   std::ranges::transform(type, type.begin(), toupper);

                                   return v1_api_content_agreements(srv, std::move(ctx), type, country, version, db, settingsMgr, certMgr);
                               });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/content/time_zones/([A-Z]{2})/([a-z]{2})$)",
                               [settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   const std::regex re(R"(^/v1/api/content/time_zones/([A-Z]{2})/([a-z]{2})$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   const std::string country = match[1];
                                   const std::string language = match[2];

                                   return v1_api_content_timezones(srv, std::move(ctx), country, language, settingsMgr, certMgr);
                               });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/people/([A-Za-z0-9\-_]+)$)",
                               [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   const std::regex re(R"(^/v1/api/people/([A-Za-z0-9\-_]+)$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   const std::string nnid = match[1];

                                   return v1_api_people_nnid(srv, std::move(ctx), nnid, db, settingsMgr, certMgr);
                               });

    server->registerRoute("account." + domain, "/v1/api/miis",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_miis(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/api/support/validate/email",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_support_validate_email(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/support/email_confirmation/([0-9]+)/([0-9]{6})$)",
                               [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   const std::regex re(R"(^/v1/api/support/email_confirmation/([0-9]+)/([0-9]{6})$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   const std::string pid = match[1];
                                   const std::string code = match[2];

                                   return v1_api_support_email_confirmation(srv, std::move(ctx), pid, code, db, settingsMgr, certMgr);
                               });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/support/forgotten_password/([0-9]+)$)",
                               [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   const std::regex re(R"(^/v1/api/support/forgotten_password/([0-9]+)$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   const std::string pid = match[1];

                                   return v1_api_support_forgotten_password(srv, std::move(ctx), pid, db, settingsMgr, certMgr);
                               });

    server->registerRoute("account." + domain, "/v1/api/support/resend_confirmation",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_support_resend_confirmation(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/support/send_confirmation/pin/([A-Za-z0-9._%+-]+[(%40)|@][A-Za-z0-9.-]+\.[A-Za-z]{2,})$)",
                               [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   const std::regex re(R"(^/v1/api/support/send_confirmation/pin/([A-Za-z0-9._%+-]+[(%40)|@][A-Za-z0-9.-]+\.[A-Za-z]{2,})$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   const std::string email = match[1];

                                   return v1_api_support_send_confirmation_pin(srv, std::move(ctx), email, db, settingsMgr, certMgr);
                               });

    server->registerRegexRoute("account." + domain, R"(^/v1/api/support/send_forgotten/pin/([A-Za-z0-9._%+-]+[(%40)|@][A-Za-z0-9.-]+\.[A-Za-z]{2,})/([0-9]{5})$)",
                               [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                   const std::regex re(R"(^/v1/api/support/send_forgotten/pin/([A-Za-z0-9._%+-]+[(%40)|@][A-Za-z0-9.-]+\.[A-Za-z]{2,})/([0-9]{5})$)");
                                   std::smatch match;
                                   const std::string path = ctx->request->getPath();
                                   std::regex_match(path, match, re);
                                   const std::string email = match[1];
                                   const std::string pin = match[2];

                                   return v1_api_support_send_forgotten_pin(srv, std::move(ctx), email, pin, db, settingsMgr, certMgr);
                               });

    server->registerRoute("account." + domain, "/v1/account-settings/ui/profile",
                  [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_account_settings_ui_profile(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerRoute("account." + domain, "/v1/account-settings/ui/profile/update",
                  [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return v1_api_account_settings_ui_profile_update(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    constexpr std::array<std::string_view, 7> miiTypes = {"normal_face", "frustrated_face", "happy_face", "like_face",
                                                          "puzzled_face", "surprised_face", "whole_body"};
    for (const auto& type : miiTypes) {
        server->registerRoute("mii-secure.account." + domain, "/" + std::string(type) + ".png",
                              [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                                  return mii_image(srv, std::move(ctx), db, settingsMgr, certMgr);
                              });
    }

    server->registerRoute("mii-secure.account." + domain, "/standard.tga",
                          [db, settingsMgr, certMgr](http::Server* srv, std::shared_ptr<http::Context> ctx) {
                              return mii_image(srv, std::move(ctx), db, settingsMgr, certMgr);
                          });

    server->registerErrorPage("account." + domain, errorHandler);
    server->registerErrorPage("mii-secure.account." + domain, errorHandler);
}

} // namespace acc