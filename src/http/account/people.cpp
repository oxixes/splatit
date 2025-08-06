#include "account.hpp"

#include <date/tz.h>
#include <cstring>

#include "../../crypto/tools.hpp"

// TODO Don't use get profile all the time, try and improve performance by executing shorter queries

namespace acc {

using namespace async;

bool checkAgreement(const pugi::xml_node& agreement) {
    if (agreement.empty()) return false; // Agreement node must not be empty

    // Check country
    if (agreement.child("country").empty()) {
        return false;
    }

    if (!timezones.contains(agreement.child_value("country"))) {
        return false;
    }

    // Check type
    if (agreement.child("type").empty()) {
        return false;
    }

    // Check version
    if (agreement.child("version").empty()) {
        return false;
    }

    // Check that version is a 4-digit number
    if (std::string version = agreement.child_value("version"); version.length() != 4 || !std::ranges::all_of(version, ::isdigit)) {
        return false;
    }

    // Check agreement date
    if (agreement.child("agreement_date").empty()) {
        return false;
    }

    // Parse the agreement date to check if it's a valid date (format YYYY-MM-DDTHH:MM:SS)
    const std::string agreementDate = agreement.child_value("agreement_date");
    try {
        date::sys_seconds parsedDate;
        std::istringstream ss{agreementDate};
        ss >> date::parse("%Y-%m-%dT%H:%M:%S", parsedDate);
        if (ss.fail()) {
            return false; // Invalid date format
        }
    } catch (const std::exception&) {
        return false; // Invalid date format
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

    // Check that address does not contain invalid characters
    if (!std::ranges::all_of(address, [](const char c) {
        return std::isalnum(c) || c == '@' || c == '.' || c == '-' || c == '_';
    })) {
            return false;
    }

    // TODO Maybe in the future check connecting to the domain

    return true;
}

bool checkEmail(const pugi::xml_node& email) {
    if (email.empty()) return false; // Email node must not be empty

    // Check address
    if (email.child("address").empty()) {
        return false;
    }

    // Check that address is a valid email
    const std::string address = email.child_value("address");
    if (!checkEmailAddress(address)) {
        return false;
    }

    // Check type
    if (email.child("type").empty() || strlen(email.child_value("type")) > 32) {
        return false;
    }

    // Check owned
    if (const std::string val = email.child_value("owned"); val != "Y" && val != "N") {
        return false;
    }

    // Check parent
    if (const std::string val = email.child_value("parent"); val != "Y" && val != "N") {
        return false;
    }

    // Check primary
    if (const std::string val = email.child_value("primary"); val != "Y" && val != "N") {
        return false;
    }

    // Check validated
    if (const std::string val = email.child_value("validated"); val != "Y" && val != "N") {
        return false;
    }

    return true;
}

bool checkMii(const pugi::xml_node& mii) {
    if (mii.empty()) return false; // Mii node must not be empty

    // Check data
    if (mii.child("data").empty()) {
        return false;
    }

    // Check that data is a valid base64 string
    const std::string data = mii.child_value("data");
    try {
        auto decoded = crypto::base64Decode(data);
        if (decoded.size() != 96) {
            return false; // Mii data must be exactly 96 bytes
        }
    } catch (const std::runtime_error&) {
        return false;
    }

    // Check name
    if (mii.child("name").empty()) {
        return false;
    }

    // Check that name is not too long
    if (strlen(mii.child_value("name")) > 32) {
        return false;
    }

    // Check primary
    if (const std::string val = mii.child_value("primary"); val != "Y" && val != "N") {
        return false;
    }

    return true;
}

bool checkDeviceAttributes(const pugi::xml_node& deviceAttributes) {
    if (deviceAttributes.empty()) return true; // No device attributes are fine

    return std::ranges::all_of(deviceAttributes.children("device_attribute"), [](const auto& attr) {
        // Check name
        if (attr.child("name").empty()) {
            return false;
        }

        if (strlen(attr.child_value("name")) > 256) {
            return false; // Name must not be longer than 64 characters
        }

        // Check value
        if (attr.child("value").empty()) {
            return false;
        }

        if (strlen(attr.child_value("value")) > 256) {
            return false; // Value must not be longer than 256 characters
        }

        return true;
    });
}

/*
 * Handler for GET https://account.<domain>/v1/api/people/<nnid>
 * Checks if a username is taken.
 */
Task<void> v1_api_people_nnid(http::Server* srv, std::shared_ptr<http::Context> ctx,
                              const std::string& nnid,
                              const std::shared_ptr<db::Database>& db,
                              const std::shared_ptr<SettingsManager>& settingsManager,
                              const std::shared_ptr<CertManager>& certManager) {
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

    if (nnid.length() < 6 || nnid.length() > 16 || !std::ranges::all_of(nnid, [](const char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    })) {
        res = createError(ctx->request->getVersion(), 101, "Bad request", "nnid", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftGetUserByUsernameCommand(nnid);
    db::Result userResults = co_await db->runCommand(ctx->scheduler, std::move(cmd));

    if (userResults.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (userResults.hasData()) {
        res = createError(ctx->request->getVersion(), 100, "Account ID already exists", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for POST https://account.<domain>/v1/api/people
 * Creates a new user account.
 * Requires a device certificate.
 */
Task<void> v1_api_people(http::Server* srv, std::shared_ptr<http::Context> ctx,
                         const std::shared_ptr<db::Database>& db,
                         const std::shared_ptr<SettingsManager>& settingsManager,
                         const std::shared_ptr<CertManager>& certManager) {
    // TODO Limit registration to 12 active accounts per device
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

    // Read the body as xml
    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unique_ptr<pugi::xml_document> doc = std::make_unique<pugi::xml_document>();
    pugi::xml_parse_result parseResult = doc->load_string(body.c_str());
    if (!parseResult) {
        res = createError(ctx->request->getVersion(), 1600, "Bad request body", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (doc->empty() || doc->child("person").empty()) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "person", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check birthdate
    if (doc->child("person").child("birth_date").empty()) {
        res = createError(ctx->request->getVersion(), 129, "Bad request body", "birth_date", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    const std::string birthDate = doc->child("person").child_value("birth_date");
    if (std::regex dateRegex(R"(^\d{4}-\d{2}-\d{2}$)"); !std::regex_match(birthDate, dateRegex)) {
        res = createError(ctx->request->getVersion(), 129, "Bad request body", "birth_date", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check user_id
    if (doc->child("person").child("user_id").empty()) {
        res = createError(ctx->request->getVersion(), 101, "Bad request body", "user_id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    const std::string userId = doc->child("person").child_value("user_id");
    if (userId.length() < 6 || userId.length() > 16 || !std::ranges::all_of(userId, [](const char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    })) {
        res = createError(ctx->request->getVersion(), 101, "Bad request body", "user_id", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check password
    if (doc->child("person").child("password").empty()) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "password", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    const std::string password = doc->child("person").child_value("password");
    if (userId == password) {
        res = createError(ctx->request->getVersion(), 1107, "Same user id as password", "password", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check country
    if (doc->child("person").child("country").empty()) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "country", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!timezones.contains(doc->child("person").child_value("country"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "country", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check language
    if (doc->child("person").child("language").empty()) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "language", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!timezones[doc->child("person").child_value("country")].contains(doc->child("person").child_value("language"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "language", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check TZ
    if (doc->child("person").child("tz_name").empty()) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "tz_name", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    bool found = false;
    for (auto& tz : timezones[doc->child("person").child_value("country")][doc->child("person").child_value("language")]) {
        if (tz["area"].get<std::string>() == doc->child("person").child_value("tz_name")) {
            found = true;
            break;
        }
    }

    if (!found) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "tz_name", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check gender
    if (const std::string val = doc->child("person").child_value("gender"); val != "M" && val != "F") {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "gender", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check marketing_flag
    if (const std::string val = doc->child("person").child_value("marketing_flag"); val != "Y" && val != "N") {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "marketing_flag", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check off_device_flag
    if (const std::string val = doc->child("person").child_value("off_device_flag"); val != "Y" && val != "N") {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "off_device_flag", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check region
    if (doc->child("person").child("region").empty()) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "region", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check that region is a valid number
    const std::string regionStr = doc->child("person").child_value("region");
    try {
        int64_t region = std::stoll(regionStr);
    } catch (const std::invalid_argument&) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "region", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    } catch (const std::out_of_range&) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "region", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check agreement
    if (!checkAgreement(doc->child("person").child("agreement"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "agreement", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check email
    if (!checkEmail(doc->child("person").child("email"))) {
        res = createError(ctx->request->getVersion(), 103, "Bad request body", "email", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check Mii
    if (!checkMii(doc->child("person").child("mii"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "mii", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check device attributes
    if (!checkDeviceAttributes(doc->child("person").child("device_attributes"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "device_attributes", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Most things are already checked right now, so we can proceed to the database stuff
    if (const std::string country = ctx->request->getHeader("x-nintendo-country")[0]; !ctx->request->hasHeader("accept-language") || !timezones[country].contains(ctx->request->getHeader("accept-language")[0])) {
        res = createError(ctx->request->getVersion(), 2, "Bad request header", "Accept-Language", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    const std::string language = ctx->request->getHeader("accept-language")[0];
    uint32_t region = std::stoul(ctx->request->getHeader("x-nintendo-region")[0]);
    const std::string serialNumber = ctx->request->getHeader("x-nintendo-serial-number")[0];
    const std::string systemVersion = ctx->request->getHeader("x-nintendo-system-version")[0];
    const std::string deviceId = ctx->request->getHeader("x-nintendo-device-id")[0];
    uint32_t deviceIdNum = std::stoul(deviceId);

    db::datetime_t lastUpdated = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    std::shared_ptr<db::Database> session = db->createSession();

    if ((co_await session->startTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    std::unique_ptr<db::Command> updateDevCmd = db::Database::craftInsertOrUpdateDeviceCommand(deviceIdNum, language, 1,
            region, serialNumber, systemVersion, "RETAIL", "USER", lastUpdated);
    db::Result deviceResults = co_await session->runCommand(ctx->scheduler, std::move(updateDevCmd));
    if (deviceResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    std::unique_ptr<db::Command> userExistsCmd = db::Database::craftGetUserByUsernameCommand(doc->child("person").child_value("user_id"));
    db::Result existingUserResults = co_await session->runCommand(ctx->scheduler, std::move(userExistsCmd));
    if (existingUserResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    if (existingUserResults.hasData()) {
        co_await session->rollbackTransaction(ctx->scheduler);
        res = createError(ctx->request->getVersion(), 100, "Account ID already exists", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // After the device is in the database, we can insert the email
    std::unique_ptr<db::Command> emailCmd = db::Database::craftInsertOrUpdateEmailCommand(std::nullopt,
        doc->child("person").child("email").child_value("address"),
        std::string(doc->child("person").child("email").child_value("parent")) == "Y",
        std::string(doc->child("person").child("email").child_value("primary")) == "Y",
        true, // reachable
        doc->child("person").child("email").child_value("type"),
        "USER",
        true, // validated. For now, we set the email as validated, but FIXME actually validate the email
        db::datetime_t());

    // We can insert the mii as well
    // For the hash, we'll generate a random string of 13 characters for now, as we don't know how that is generated
    std::string miiHash = crypto::genRandomString(13, "abcdefghijklmnopqrstuvwxyz0123456789");
    std::unique_ptr<db::Command> miiCmd = db::Database::craftInsertOrUpdateMiiCommand(std::nullopt,
        miiHash,
        doc->child("person").child("mii").child_value("name"),
        std::string(doc->child("person").child("mii").child_value("primary")) == "Y",
        doc->child("person").child("mii").child_value("data"));

    // And finally we can also get the latest pid to get a new one for the user
    std::unique_ptr<db::Command> pidCmd = db::Database::craftGetLatestPidCommand();

    db::Result emailResults = co_await session->runCommand(ctx->scheduler, std::move(emailCmd));
    db::Result miiResults = co_await session->runCommand(ctx->scheduler, std::move(miiCmd));
    db::Result pidResults = co_await session->runCommand(ctx->scheduler, std::move(pidCmd));

    if (emailResults.getStatus() != db::DBResultStatus::SUCCESS ||
        miiResults.getStatus() != db::DBResultStatus::SUCCESS ||
        pidResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    auto emailId = emailResults.getData<int64_t>();
    auto miiId = miiResults.getData<int64_t>();
    uint32_t pid = pidResults.getData<uint32_t>() - 1; // Pid is the latest pid, so we need to subtract 1 to get the new pid

    std::string hashedPassword;
    bool error = false;
    try {
        std::string nintendoPasswordHash = crypto::genNintendoPasswordHash(pid, doc->child("person").child_value("password"));
        std::string salt = crypto::genSalt();
        hashedPassword = crypto::hashPassword(nintendoPasswordHash, salt);
    } catch (const std::exception& e) {
        error = true;
    }

    // Compilers don't like co_await inside a catch
    if (error) {
        co_await session->rollbackTransaction(ctx->scheduler);
        res = createError(ctx->request->getVersion(), 2001, "Internal server error", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    db::datetime_t created = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Now we can insert the user
    std::unique_ptr<db::Command> profileCmd = db::Database::craftInsertProfileCommand(
        pid,
        doc->child("person").child_value("user_id"),
        hashedPassword,
        emailId,
        miiId,
        std::string(doc->child("person").child_value("gender")) == "F",
        std::stoll(doc->child("person").child_value("region")),
        doc->child("person").child_value("tz_name"),
        doc->child("person").child_value("language"),
        true, // active
        std::string(doc->child("person").child_value("marketing_flag")) == "Y",
        std::string(doc->child("person").child_value("off_device_flag")) == "Y",
        doc->child("person").child_value("birth_date"),
        doc->child("person").child_value("country"),
        created,
        created); // created and updated are the same for now

    db::Result profileResults = co_await session->runCommand(ctx->scheduler, std::move(profileCmd));
    if (profileResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    std::unique_ptr<db::Command> agreementCmd = db::Database::craftInsertOrUpdateUserAgreementCommand(
        pid,
        doc->child("person").child("agreement").child_value("type"),
        std::stol(doc->child("person").child("agreement").child_value("version")),
        doc->child("person").child("agreement").child_value("country"),
        now); // Agreement date is now for now, we could parse it from the XML, but it's not necessary

    std::unique_ptr<db::Command> ownershipCmd = db::Database::craftInsertOrUpdateOwnershipCommand(
        pid,
        deviceIdNum,
        "ACTIVE",
        now);

    std::vector<ManualTask<db::Result>> tasks;
    tasks.push_back(session->runCommand(ctx->scheduler, std::move(agreementCmd)));
    tasks.push_back(session->runCommand(ctx->scheduler, std::move(ownershipCmd)));

    for (const auto& attr : doc->child("person").child("device_attributes").children("device_attribute")) {
        std::unique_ptr<db::Command> deviceAttrCmd = db::Database::craftInsertOrUpdateDeviceAttributesCommand(
            deviceIdNum,
            pid,
            attr.child_value("name"),
            attr.child_value("value"),
            now);
        tasks.push_back(session->runCommand(ctx->scheduler, std::move(deviceAttrCmd)));
    }

    for (std::vector<db::Result> results = co_await waitForAll(std::move(tasks)); const auto& result : results) {
        if (result.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction(ctx->scheduler);
            throw std::runtime_error("Database error");
        }
    }

    if ((co_await session->commitTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    // Prepare the response
    pugi::xml_document responseDoc;
    pugi::xml_node personNode = responseDoc.append_child("person");
    personNode.append_child("pid").text().set(std::to_string(pid).c_str());

    res = prepareResponse(ctx->request->getVersion(), responseDoc, HTTP_STATUS_CREATED);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for PUT https://account.<domain>/v1/api/people/@me
 * Updates the profile of the logged in user.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
Task<void> v1_api_people_me(http::Server* srv, std::shared_ptr<http::Context> ctx,
                            const std::shared_ptr<db::Database>& db,
                            const std::shared_ptr<SettingsManager>& settingsManager,
                            const std::shared_ptr<CertManager>& certManager) {
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

    crypto::AccountToken accountToken;
    if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Read the body as xml
    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unique_ptr<pugi::xml_document> doc = std::make_unique<pugi::xml_document>();
    pugi::xml_parse_result parseResult = doc->load_string(body.c_str());
    if (!parseResult) {
        res = createError(ctx->request->getVersion(), 1600, "Bad request body", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (doc->empty() || doc->child("person").empty()) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "person", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::optional<bool> gender = std::nullopt;
    if (!doc->child("person").child("gender").empty()) {
        std::string genderStr = doc->child("person").child_value("gender");
        if (genderStr != "M" && genderStr != "F") {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "gender", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        gender = genderStr == "F";
    }

    std::optional<int64_t> region = std::nullopt;
    if (!doc->child("person").child("region").empty()) {
        try {
            region = std::stoll(doc->child("person").child_value("region"));
        } catch (const std::invalid_argument&) {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "region", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        } catch (const std::out_of_range&) {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "region", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    std::optional<std::string> country = std::nullopt;
    if (!doc->child("person").child("country").empty()) {
        country = doc->child("person").child_value("country");
        if (!timezones.contains(*country)) {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "country", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    std::string countryToCheck = country.has_value() ? *country : ctx->request->getHeader("x-nintendo-country")[0];

    std::optional<std::string> language = std::nullopt;
    if (!ctx->request->hasHeader("accept-language") || !timezones[countryToCheck].contains(ctx->request->getHeader("accept-language")[0])) {
        res = createError(ctx->request->getVersion(), 2, "Bad request header", "Accept-Language", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!doc->child("person").child("language").empty()) {
        language = doc->child("person").child_value("language");
        if (!timezones[countryToCheck].contains(*language)) {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "language", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    std::string languageToCheck = language.has_value() ? *language : ctx->request->getHeader("accept-language")[0];

    std::optional<std::string> timezone = std::nullopt;
    if (!doc->child("person").child("tz_name").empty()) {
        timezone = doc->child("person").child_value("tz_name");
        bool found = false;
        for (const auto& tz : timezones[countryToCheck][languageToCheck]) {
            if (tz["area"].get<std::string>() == *timezone) {
                found = true;
                break;
            }
        }

        if (!found) {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "tz_name", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    std::optional<bool> marketing = std::nullopt;
    if (!doc->child("person").child("marketing_flag").empty()) {
        std::string marketingStr = doc->child("person").child_value("marketing_flag");
        if (marketingStr != "Y" && marketingStr != "N") {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "marketing_flag", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        marketing = marketingStr == "Y";
    }

    std::optional<bool> offDevice = std::nullopt;
    if (!doc->child("person").child("off_device_flag").empty()) {
        std::string offDeviceStr = doc->child("person").child_value("off_device_flag");
        if (offDeviceStr != "Y" && offDeviceStr != "N") {
            res = createError(ctx->request->getVersion(), 2, "Bad request body", "off_device_flag", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
        offDevice = offDeviceStr == "Y";
    }

    std::optional<std::string> password = std::nullopt;
    if (!doc->child("person").child("password").empty()) {
        password = doc->child("person").child_value("password");
        try {
            std::string nintendoPasswordHash = crypto::genNintendoPasswordHash(accountToken.pid, *password);
            std::string salt = crypto::genSalt();
            *password = crypto::hashPassword(nintendoPasswordHash, salt);
        } catch (const std::exception& e) {
            res = createError(ctx->request->getVersion(), 2001, "Internal server error", "", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    if (!doc->child("person").child("email").empty()) {
        if (!checkEmail(doc->child("person").child("email"))) {
            res = createError(ctx->request->getVersion(), 103, "Bad request body", "email", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto session = db->createSession();
        if ((co_await session->startTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
            throw std::runtime_error("Database error");
        }

        auto currentDataCmd = db::Database::craftGetUserProfileCommand(accountToken.pid);
        auto emailCmd = db::Database::craftInsertOrUpdateEmailCommand(
            std::nullopt, // emailId
            doc->child("person").child("email").child_value("address"),
            std::string(doc->child("person").child("email").child_value("parent")) == "Y",
            std::string(doc->child("person").child("email").child_value("primary")) == "Y",
            true, // reachable
            doc->child("person").child("email").child_value("type"),
            "USER",
            true, // validated - we set the email as validated for now, but FIXME actually validate the email
            db::datetime_t());

        auto currentDataResults = co_await session->runCommand(ctx->scheduler, std::move(currentDataCmd));
        if (currentDataResults.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction(ctx->scheduler);
            throw std::runtime_error("Database error");
        }

        if (!currentDataResults.hasData()) {
            co_await session->rollbackTransaction(ctx->scheduler);
            res = createError(ctx->request->getVersion(), 130, "Account not found", "", HTTP_STATUS_NOT_FOUND);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        auto emailResults = co_await session->runCommand(ctx->scheduler, std::move(emailCmd));
        if (emailResults.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction(ctx->scheduler);
            throw std::runtime_error("Database error");
        }

        int64_t oldEmailId = currentDataResults.getData<db::DBUserProfileData>().emailId;

        auto cmd = db::Database::craftUpdateUserProfileCommand(
            accountToken.pid,
            std::nullopt, // username
            password,
            emailResults.getData<int64_t>(), // emailId
            std::nullopt, // mii
            gender,
            region,
            timezone,
            language,
            std::nullopt, // active
            marketing,
            offDevice,
            std::nullopt, // birthdate
            country,
            std::nullopt, // created
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated

        db::Result results = co_await session->runCommand(ctx->scheduler, std::move(cmd));
        if (results.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction(ctx->scheduler);
            throw std::runtime_error("Database error");
        }

        std::unique_ptr<db::Command> deleteEmailCmd = db::Database::craftDeleteEmailCommand(oldEmailId);
        if ((co_await session->runCommand(ctx->scheduler, std::move(deleteEmailCmd))).getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction(ctx->scheduler);
            throw std::runtime_error("Database error");
        }

        if ((co_await session->commitTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
            throw std::runtime_error("Database error");
        }

        res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    } else {
        auto cmd = db::Database::craftUpdateUserProfileCommand(
            accountToken.pid,
            std::nullopt, // username
            password,
            std::nullopt, // email
            std::nullopt, // mii
            gender,
            region,
            timezone,
            language,
            std::nullopt, // active
            marketing,
            offDevice,
            std::nullopt, // birthdate
            country,
            std::nullopt, // created
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated

        if ((co_await db->runCommand(ctx->scheduler, std::move(cmd))).getStatus() != db::DBResultStatus::SUCCESS) {
            throw std::runtime_error("Database error");
        }

        res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }
}

/*
 * Handler for GET https://account.<domain>/v1/api/people/@me/emails
 * Obtains the email addresses of the logged in user.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
Task<void> v1_api_people_me_emails(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                   const std::shared_ptr<db::Database>& db,
                                   const std::shared_ptr<SettingsManager>& settingsManager,
                                   const std::shared_ptr<CertManager>& certManager) {
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

    crypto::AccountToken accountToken;
    if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftGetUserProfileCommand(accountToken.pid);
    db::Result profileResults = co_await db->runCommand(ctx->scheduler, std::move(cmd));
    if (profileResults.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (!profileResults.hasData()) {
        res = createError(ctx->request->getVersion(), 130, "Account not found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto userProfile = std::move(profileResults.getData<db::DBUserProfileData>());

    pugi::xml_document doc;
    pugi::xml_node emailsNode = doc.append_child("emails");

    pugi::xml_node emailNode = emailsNode.append_child("email");
    emailNode.append_child("id").text().set(std::to_string(userProfile.emailId).c_str());
    emailNode.append_child("address").text().set(userProfile.email.c_str(), userProfile.email.length());
    emailNode.append_child("parent").text().set(userProfile.emailParent ? "Y" : "N", 1);
    emailNode.append_child("primary").text().set(userProfile.emailPrimary ? "Y" : "N", 1);
    emailNode.append_child("reachable").text().set(userProfile.emailReachable ? "Y" : "N", 1);
    emailNode.append_child("type").text().set(userProfile.emailType.c_str(), userProfile.emailType.length());
    emailNode.append_child("updated_by").text().set(userProfile.emailUpdatedBy.c_str(), userProfile.emailUpdatedBy.length());
    emailNode.append_child("validated").text().set(userProfile.emailValidated ? "Y" : "N", 1);

    if (userProfile.emailValidated) {
        std::string emailValidatedDateStr = util::getDateISO8601(userProfile.emailValidatedDate);
        emailNode.append_child("validated_date").text().set(emailValidatedDateStr.c_str(), emailValidatedDateStr.length());
    } else {
        // Insert an empty validated_date node if the email is not validated
        emailNode.append_child("validated_date");
    }

    res = prepareResponse(ctx->request->getVersion(), doc, HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for PUT https://account.<domain>/v1/api/people/@me/miis/@primary
 * Updates the primary Mii of the logged in user.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
Task<void> v1_api_people_me_miis_primary(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                         const std::shared_ptr<db::Database>& db,
                                         const std::shared_ptr<SettingsManager>& settingsManager,
                                         const std::shared_ptr<CertManager>& certManager) {
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

    crypto::AccountToken accountToken;
    if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Read the body as xml
    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unique_ptr<pugi::xml_document> doc = std::make_unique<pugi::xml_document>();
    pugi::xml_parse_result parseResult = doc->load_string(body.c_str());
    if (!parseResult) {
        res = createError(ctx->request->getVersion(), 1600, "Bad request body", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!checkMii(doc->child("mii"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "mii", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    auto session = db->createSession();
    if ((co_await session->startTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    std::string miiHash = crypto::genRandomString(13, "abcdefghijklmnopqrstuvwxyz0123456789");

    std::unique_ptr<db::Command> currentDataCmd = db::Database::craftGetUserProfileCommand(accountToken.pid);
    db::Result currentDataResults = co_await session->runCommand(ctx->scheduler, std::move(currentDataCmd));
    if (currentDataResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    if (!currentDataResults.hasData()) {
        co_await session->rollbackTransaction(ctx->scheduler);
        res = createError(ctx->request->getVersion(), 130, "Account not found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<db::Command> miiCmd = db::Database::craftInsertOrUpdateMiiCommand(
        std::nullopt, // miiId
        miiHash,
        doc->child("mii").child_value("name"),
        std::string(doc->child("mii").child_value("primary")) == "Y",
        doc->child("mii").child_value("data"));
    db::Result miiResults = co_await session->runCommand(ctx->scheduler, std::move(miiCmd));
    if (miiResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    int64_t oldMiiId = currentDataResults.getData<db::DBUserProfileData>().miiId;

    // Update the user profile with the new Mii
    std::unique_ptr<db::Command> cmd = db::Database::craftUpdateUserProfileCommand(
        accountToken.pid,
        std::nullopt, // username
        std::nullopt, // password
        std::nullopt, // email
        miiResults.getData<int64_t>(), // miiId
        std::nullopt, // gender
        std::nullopt, // region
        std::nullopt, // timezone
        std::nullopt, // language
        std::nullopt, // active
        std::nullopt, // marketing
        std::nullopt, // offDevice
        std::nullopt, // birthdate
        std::nullopt, // country
        std::nullopt, // created
        std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated

    if ((co_await session->runCommand(ctx->scheduler, std::move(cmd))).getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    auto deleteMiiCmd = db::Database::craftDeleteMiiCommand(oldMiiId);
    if ((co_await session->runCommand(ctx->scheduler, std::move(deleteMiiCmd))).getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    if ((co_await session->commitTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for POST https://account.<domain>/v1/api/people/@me/devices/@current/attributes
 * Updates the attributes of the current device of the logged-in user.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
Task<void> v1_api_people_me_devices_current_attributes(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                       const std::shared_ptr<db::Database>& db,
                                                       const std::shared_ptr<SettingsManager>& settingsManager,
                                                       const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_POST) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    crypto::AccountToken accountToken;
    if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Read the body as xml
    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unique_ptr<pugi::xml_document> doc = std::make_unique<pugi::xml_document>();
    pugi::xml_parse_result parseResult = doc->load_string(body.c_str());
    if (!parseResult) {
        res = createError(ctx->request->getVersion(), 1600, "Bad request body", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Check the attributes
    if (!checkDeviceAttributes(doc->child("device_attributes"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "device_attributes", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto session = db->createSession();
    if ((co_await session->startTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    auto ownershipCmd = db::Database::craftGetOwnershipCommand(accountToken.pid, accountToken.deviceId);
    auto ownershipResults = co_await session->runCommand(ctx->scheduler, std::move(ownershipCmd));
    if (ownershipResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    if (!ownershipResults.hasData() || ownershipResults.getData<db::DBOwnershipData>().status != "ACTIVE") {
        co_await session->rollbackTransaction(ctx->scheduler);
        res = createError(ctx->request->getVersion(), 104, "Device not linked to this account", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::vector<ManualTask<db::Result>> tasks;
    for (const auto& attribute : doc->child("device_attributes").children("device_attribute")) {
        auto cmd = db::Database::craftInsertOrUpdateDeviceAttributesCommand(
            accountToken.deviceId,
            accountToken.pid,
            attribute.child_value("name"),
            attribute.child_value("value"),
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated

        tasks.push_back(session->runCommand(ctx->scheduler, std::move(cmd)));
    }

    if (tasks.empty()) {
        // Return 200 OK if no attributes were provided
        co_await session->commitTransaction(ctx->scheduler);
        res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::vector<db::Result> results = co_await waitForAll(std::move(tasks));
    for (const auto& result : results) {
        if (result.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction(ctx->scheduler);
            throw std::runtime_error("Database error");
        }
    }

    if ((co_await session->commitTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for POST https://account.<domain>/v1/api/people/@me/agreements
 * Adds or updates a user agreement for the logged-in user.
 * Requires authentication with a HashedBasic Authentication header.
 */
Task<void> v1_api_people_me_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                       const std::shared_ptr<db::Database>& db,
                                       const std::shared_ptr<SettingsManager>& settingsManager,
                                       const std::shared_ptr<CertManager>& certManager) {
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

    std::optional<uint32_t> pid = co_await checkHashedBasicAuth(db, ctx);
    if (!pid.has_value()) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Read the body as xml
    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unique_ptr<pugi::xml_document> doc = std::make_unique<pugi::xml_document>();
    pugi::xml_parse_result parseResult = doc->load_string(body.c_str());
    if (!parseResult) {
        res = createError(ctx->request->getVersion(), 1600, "Bad request body", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!checkAgreement(doc->child("agreement"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "agreement", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftInsertOrUpdateUserAgreementCommand(
        *pid,
        doc->child("agreement").child_value("type"),
        std::stol(doc->child("agreement").child_value("version")),
        doc->child("agreement").child_value("country"),
        std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // signedAt

    db::Result results = co_await db->runCommand(ctx->scheduler, std::move(cmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/people/@me/profile
 * Obtains the profile of the user with the given principal id.
 * Requires authentication with an access token generated at /v1/api/oauth20/access_token/generate.
 */
Task<void> v1_api_people_me_profile(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                    const std::shared_ptr<db::Database>& db,
                                    const std::shared_ptr<SettingsManager>& settingsManager,
                                    const std::shared_ptr<CertManager>& certManager,
                                    std::optional<uint32_t> pid) {
    if (!pid.has_value()) {
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

        crypto::AccountToken accountToken;
        if (!checkOauthToken(ctx->request, settingsManager, accountToken)) {
            res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }

        pid = accountToken.pid;
    }

    std::unique_ptr<db::Command> profileCmd = db::Database::craftGetUserProfileCommand(pid.value());
    db::Result profileResults = co_await db->runCommand(ctx->scheduler, std::move(profileCmd));
    if (profileResults.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (!profileResults.hasData()) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 130, "Account not found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto ownershipCmd = db::Database::craftGetLatestOwnershipCommand(pid.value());
    db::Result ownershipResults = co_await db->runCommand(ctx->scheduler, std::move(ownershipCmd));
    if (ownershipResults.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (!ownershipResults.hasData()) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                          "Account with PID " + std::to_string(pid.value()) + " has no device ownerships! This means the user won't be able to do certain actions. This should NOT happen under normal circumstances.");
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 2001, "Internal server error", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto ownershipData = ownershipResults.getData<db::DBOwnershipData>();

    std::unique_ptr<db::Command> devAttrsCmd = db::Database::craftGetDeviceAttributesCommand(pid.value(), ownershipData.deviceId);
    db::Result deviceResults = co_await db->runCommand(ctx->scheduler, std::move(devAttrsCmd));
    if (deviceResults.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    auto userProfile = std::move(profileResults.getData<db::DBUserProfileData>());
    auto deviceAttributes = std::move(deviceResults.getData<std::vector<db::DBDeviceAttributeData>>());

    pugi::xml_document doc;
    pugi::xml_node person = doc.append_child("person");
    person.append_child("active_flag").text().set((userProfile.active) ? "Y" : "N", 1);
    person.append_child("birth_date").text().set(userProfile.birthdate.c_str(), userProfile.birthdate.length());
    person.append_child("country").text().set(userProfile.country.c_str(), userProfile.country.length());

    std::string createDate = util::getDateISO8601(userProfile.created);
    person.append_child("create_date").text().set(createDate.c_str(), createDate.length());

    person.append_child("gender").text().set((userProfile.gender) ? "F" : "M", 1);
    person.append_child("language").text().set(userProfile.language.c_str(), userProfile.language.length());

    std::string updatedDate = util::getDateISO8601(userProfile.updated);
    person.append_child("updated").text().set(updatedDate.c_str(), updatedDate.length());

    person.append_child("marketing_flag").text().set((userProfile.marketing) ? "Y" : "N", 1);
    person.append_child("off_device_flag").text().set((userProfile.offDevice) ? "Y" : "N", 1);

    std::string pidStr = std::to_string(userProfile.pid);
    person.append_child("pid").text().set(pidStr.c_str(), pidStr.length());

    std::string regionStr = std::to_string(userProfile.region);
    person.append_child("region").text().set(regionStr.c_str(), regionStr.length());

    person.append_child("tz_name").text().set(userProfile.tz.c_str(), userProfile.tz.length());
    person.append_child("user_id").text().set(userProfile.username.c_str(), userProfile.username.length());

    // Calculate UTC offset in seconds from the timezone name

    // ATTENTION Code explorer!
    // You have stumbled upon a time and timezone related section of the code!
    // These sections are probably one of the worst a developer can deal with.
    // The code below uses the date library to get the current offset. It CAN'T be stored
    // as a static value, because the offset can change due to daylight saving time and the
    // real server takes this into account.

    int64_t utcOffset = 0;

    auto tz = date::locate_zone(userProfile.tz);
    if (!tz) {
        ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT,
                         "Invalid timezone " + userProfile.tz + " for user with PID " + std::to_string(userProfile.pid));
    } else {
        auto now = std::chrono::system_clock::now();
        date::zoned_time zt{tz, now};
        auto info = zt.get_info();
        utcOffset = std::chrono::duration_cast<std::chrono::seconds>(info.offset).count();
    }

    person.append_child("utc_offset").text().set(std::to_string(utcOffset).c_str(), std::to_string(utcOffset).length());

    pugi::xml_node deviceAttributesNode = person.append_child("device_attributes");
    for (const auto& attr : deviceAttributes) {
        pugi::xml_node deviceAttribute = deviceAttributesNode.append_child("device_attribute");
        std::string createdDateStr = util::getDateISO8601(attr.createdDate);
        deviceAttribute.append_child("created_date").text().set(createdDateStr.c_str(), createdDateStr.length());
        deviceAttribute.append_child("name").text().set(attr.name.c_str(), attr.name.length());
        deviceAttribute.append_child("value").text().set(attr.value.c_str(), attr.value.length());
    }

    pugi::xml_node email = person.append_child("email");
    email.append_child("address").text().set(userProfile.email.c_str(), userProfile.email.length());

    std::string emailIdStr = std::to_string(userProfile.emailId);
    email.append_child("id").text().set(emailIdStr.c_str(), emailIdStr.length());

    email.append_child("parent").text().set((userProfile.emailParent) ? "Y" : "N", 1);
    email.append_child("primary").text().set((userProfile.emailPrimary) ? "Y" : "N", 1);
    email.append_child("reachable").text().set((userProfile.emailReachable) ? "Y" : "N", 1);
    email.append_child("type").text().set(userProfile.emailType.c_str(), userProfile.emailType.length());
    email.append_child("updated_by").text().set(userProfile.emailUpdatedBy.c_str(), userProfile.emailUpdatedBy.length());
    email.append_child("validated").text().set((userProfile.emailValidated) ? "Y" : "N", 1);

    if (userProfile.emailValidated) {
        std::string emailValidatedDateStr = util::getDateISO8601(userProfile.emailValidatedDate);
        email.append_child("validated_date").text().set(emailValidatedDateStr.c_str(), emailValidatedDateStr.length());
    } else {
        // Insert an empty validated_date node if the email is not validated
        email.append_child("validated_date");
    }

    pugi::xml_node mii = person.append_child("mii");
    mii.append_child("data").text().set(userProfile.miiData.c_str(), userProfile.miiData.length());

    std::string miiStatus = "COMPLETED";
    mii.append_child("status").text().set(miiStatus.c_str(), miiStatus.length());

    std::string miiIdStr = std::to_string(userProfile.miiId);
    mii.append_child("id").text().set(miiIdStr.c_str(), miiIdStr.length());

    mii.append_child("name").text().set(userProfile.miiName.c_str(), userProfile.miiName.length());
    mii.append_child("mii_hash").text().set(userProfile.miiHash.c_str(), userProfile.miiHash.length());
    mii.append_child("primary").text().set((userProfile.miiPrimary) ? "Y" : "N", 1);

    pugi::xml_node miiImages = mii.append_child("mii_images");
    pugi::xml_node miiImage = miiImages.append_child("mii_image");

    std::string miiType = "standard"; // Profile images are always standard
    miiImage.append_child("type").text().set(miiType.c_str(), miiType.length());

    std::string miiImageId = std::to_string(userProfile.miiId); // This should be its own id, but we'll use the mii id for now
    miiImage.append_child("id").text().set(miiImageId.c_str(), miiImageId.length());

    std::string miiImageUrl = "https://mii-secure.account." + settingsManager->getTopDomain() + "/standard.tga" +
                              "?id=" + std::to_string(userProfile.miiId);

    miiImage.append_child("url").text().set(miiImageUrl.c_str(), miiImageUrl.length());
    miiImage.append_child("cached_url").text().set(miiImageUrl.c_str(), miiImageUrl.length());

    std::unique_ptr<http::Response> res = prepareResponse(ctx->request->getVersion(), doc);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/people/@me/devices/owner
 * Obtains the profile and device data of the device that owns the logged-in user.
 * Requires authentication with a HashedBasic Authentication header.
 */
Task<void> v1_api_people_me_devices_owner(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                          const std::shared_ptr<db::Database>& db,
                                          std::shared_ptr<SettingsManager> settingsManager,
                                          std::shared_ptr<CertManager> certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::optional<uint32_t> pid = co_await checkHashedBasicAuth(db, ctx);
    if (!pid.has_value()) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    co_await v1_api_people_me_profile(srv, std::move(ctx), db, settingsManager, certManager, pid);
}

/*
 * Handler for POST https://account.<domain>/v1/api/people/@me/devices
 * Links a device to the logged-in user.
 * Requires authentication with a HashedBasic Authentication header.
 */
Task<void> v1_api_people_me_devices_post(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                         std::shared_ptr<db::Database> db,
                                         std::shared_ptr<SettingsManager> settingsManager,
                                         std::shared_ptr<CertManager> certManager) {
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

    std::optional<uint32_t> pid = co_await checkHashedBasicAuth(db, ctx);
    if (!pid.has_value()) {
        res = createError(ctx->request->getVersion(), 5, "Invalid access token", "access_token", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Read the body as xml
    std::string body(ctx->request->getBody().begin(), ctx->request->getBody().end());
    std::unique_ptr<pugi::xml_document> doc = std::make_unique<pugi::xml_document>();
    pugi::xml_parse_result parseResult = doc->load_string(body.c_str());
    if (!parseResult) {
        res = createError(ctx->request->getVersion(), 1600, "Bad request body", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!checkDeviceAttributes(doc->child("device_attributes"))) {
        res = createError(ctx->request->getVersion(), 2, "Bad request body", "device_attributes", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto session = db->createSession();
    if ((co_await session->startTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    // Check if the user already has a device linked
    auto ownershipCmd = db::Database::craftHasActiveOwnershipCommand(*pid);
    auto ownershipResults = co_await session->runCommand(ctx->scheduler, std::move(ownershipCmd));
    if (ownershipResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    if (ownershipResults.hasData() && ownershipResults.getData<bool>()) {
        co_await session->rollbackTransaction(ctx->scheduler);
        res = createError(ctx->request->getVersion(), 115, "Device already linked to this account", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Insert the device
    if (const std::string country = ctx->request->getHeader("x-nintendo-country")[0]; !ctx->request->hasHeader("accept-language") || !timezones[country].contains(ctx->request->getHeader("accept-language")[0])) {
        co_await session->rollbackTransaction(ctx->scheduler);
        res = createError(ctx->request->getVersion(), 2, "Bad request header", "Accept-Language", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    const std::string language = ctx->request->getHeader("accept-language")[0];
    uint32_t region = std::stoul(ctx->request->getHeader("x-nintendo-region")[0]);
    const std::string serialNumber = ctx->request->getHeader("x-nintendo-serial-number")[0];
    const std::string systemVersion = ctx->request->getHeader("x-nintendo-system-version")[0];
    const std::string deviceId = ctx->request->getHeader("x-nintendo-device-id")[0];
    uint32_t deviceIdNum = std::stoul(deviceId);

    db::datetime_t lastUpdated = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    auto cmd = db::Database::craftInsertOrUpdateDeviceCommand(
        deviceIdNum,
        language,
        1,
        region,
        serialNumber,
        systemVersion,
        "RETAIL",
        "USER",
        lastUpdated);
    db::Result deviceResults = co_await session->runCommand(ctx->scheduler, std::move(cmd));
    if (deviceResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    // Insert the ownership
    auto insertOwnershipCmd = db::Database::craftInsertOrUpdateOwnershipCommand(
        *pid,
        deviceIdNum,
        "ACTIVE",
        std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));
    if ((co_await session->runCommand(ctx->scheduler, std::move(insertOwnershipCmd))).getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction(ctx->scheduler);
        throw std::runtime_error("Database error");
    }

    std::vector<ManualTask<db::Result>> tasks;
    for (const auto& attribute : doc->child("device_attributes").children("device_attribute")) {
        auto devAttrCmd = db::Database::craftInsertOrUpdateDeviceAttributesCommand(
            deviceIdNum,
            *pid,
            attribute.child_value("name"),
            attribute.child_value("value"),
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated

        tasks.push_back(session->runCommand(ctx->scheduler, std::move(devAttrCmd)));
    }

    std::vector<db::Result> results = co_await waitForAll(std::move(tasks));
    for (const auto& result : results) {
        if (result.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction(ctx->scheduler);
            throw std::runtime_error("Database error");
        }
    }

    if ((co_await session->commitTransaction(ctx->scheduler)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    co_await v1_api_people_me_profile(srv, std::move(ctx), db, settingsManager, certManager, pid);
}

} // namespace acc