#include "account.hpp"

#include "../../crypto/tools.hpp"

namespace acc {

using namespace async;

std::unique_ptr<http::Response> createAccSettingsError(http::Version version, const std::string& message, const int httpStatus) {
    auto res = prepareResponse(version, httpStatus);
    res->setBody(std::vector<uint8_t>(message.begin(), message.end()));
    res->setHeader("Content-Type", "text/plain;charset=UTF-8");
    return res;
}

/*
 * Handler for GET https://account.<domain>/v1/account-settings/ui/profile
 * Provides the UI for the account settings
 * Requires authentication with a service token generated at /v1/api/provider/service_token/@me.
 */
Task<void> v1_api_account_settings_ui_profile(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                              std::shared_ptr<db::Database> db,
                                              std::shared_ptr<SettingsManager> settingsManager,
                                              std::shared_ptr<crypto::CertManager> certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createAccSettingsError(ctx->request->getVersion(), "Method Not Allowed", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!ctx->request->hasHeader("x-nintendo-service-token")) {
        res = createAccSettingsError(ctx->request->getVersion(), "Missing service token", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string jwtToken = ctx->request->getHeader("x-nintendo-service-token")[0];

    if (!crypto::verifyJWT(settingsManager->getNEXTokenKey(), jwtToken)) {
        res = createAccSettingsError(ctx->request->getVersion(), "Invalid service token", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string jwtData = jwtToken.substr(jwtToken.find('.') + 1);
    jwtData = jwtData.substr(0, jwtData.find('.'));

    auto jwtJson = nlohmann::json::parse(crypto::base64UrlDecode(jwtData));
    if (time(nullptr) > jwtJson["exp"].get<time_t>() || jwtJson["iss"].get<std::string>() != "account"
        || !jwtJson.contains("clientId") || jwtJson["clientId"].get<std::string>() != "3f3928cc6f780638d360f0485cef973f") {
        res = createAccSettingsError(ctx->request->getVersion(), "Invalid service token", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    uint32_t pid = jwtJson["sub"].get<uint32_t>();

    // Get user info
    auto getUserCmd = db::Database::craftGetUserProfileCommand(pid);
    auto getUserResults = co_await db->runCommand(std::move(getUserCmd));
    if (getUserResults.getStatus() != db::DBResultStatus::SUCCESS) {
        res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!getUserResults.hasData() || !getUserResults.getData<db::DBUserProfileData>().active) {
        res = createAccSettingsError(ctx->request->getVersion(), "User not found", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    auto userProfile = getUserResults.getData<db::DBUserProfileData>();

    json userConfig = {
        {"emailConfirmed", userProfile.emailValidated},
        {"username", userProfile.username},
        {"currentEmail", userProfile.email},
        {"dateOfBirth", userProfile.birthdate},
        {"currentGender", userProfile.gender == false ? "male" : "female"},
        {"country", userProfile.country},
        {"notificationEnabled", userProfile.marketing ? "enabled" : "disabled"},
        {"deviceAccessEnabled", userProfile.offDevice ? "enabled" : "disabled"},
        {"currentTimezone", userProfile.tz},
        {"currentRegion", std::to_string(userProfile.region)},
        {"regionOptions", {}},
        {"timezoneOptions", {}}
    };

    // Search region by country code
    bool foundRegions = false;
    for (auto country : regions) {
        if (country["code"] == userProfile.country) {
            foundRegions = true;
            for (auto region : country["regions"]) {
                userConfig["regionOptions"].push_back({{"value", std::to_string(region["id"].get<int>())}, {"label", region["name"]}});
            }
        }
    }

    if (!foundRegions) {
        userConfig["regionOptions"].push_back({{"value", std::to_string(userProfile.region)}, {"label", "No regions found for your country"}});
    }

    // Get timezones
    if (timezones.contains(userProfile.country)) {
        if (timezones[userProfile.country].contains("en")) {
            for (auto timezone : timezones[userProfile.country]["en"]) {
                userConfig["timezoneOptions"].push_back({{"value", timezone["area"].get<std::string>()}, {"label", timezone["name"].get<std::string>()}});
            }
        } else if (timezones[userProfile.country].contains(userProfile.language)) {
            for (auto timezone : timezones[userProfile.country][userProfile.language]) {
                userConfig["timezoneOptions"].push_back({{"value", timezone["area"].get<std::string>()}, {"label", timezone["name"].get<std::string>()}});
            }
        } else {
            userConfig["timezoneOptions"].push_back({{"value", userProfile.tz}, {"label", userProfile.tz}});
        }
    } else {
        userConfig["timezoneOptions"].push_back({{"value", userProfile.tz}, {"label", userProfile.tz}});
    }

    if (countriesAndLanguages["countries"].contains(userProfile.country)) {
        userConfig["country"] = countriesAndLanguages["countries"][userProfile.country]["name"];
    }

    std::string html = accountSettingsHTML;
    // Replace {{CONFIG_JSON}} with the userConfig json
    html.replace(html.find("{{CONFIG_JSON}}"), 15, userConfig.dump());

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    res->setHeader("Content-Type", "text/html;charset=UTF-8");
    res->setBody(std::vector<uint8_t>(html.begin(), html.end()));
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for POST https://account.<domain>/v1/account-settings/ui/profile/update
 * Updates the user profile with the given data
 * Requires authentication with a service token generated at /v1/api/provider/service_token/@me.
 */
Task<void> v1_api_account_settings_ui_profile_update(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                     std::shared_ptr<db::Database> db,
                                                     std::shared_ptr<SettingsManager> settingsManager,
                                                     std::shared_ptr<crypto::CertManager> certManager) {
    if (ctx->request->getMethod() != http::Method::M_POST) {
        std::unique_ptr<http::Response> res = createAccSettingsError(ctx->request->getVersion(), "Method Not Allowed", HTTP_STATUS_METHOD_NOT_ALLOWED);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!ctx->request->hasHeader("x-nintendo-service-token")) {
        res = createAccSettingsError(ctx->request->getVersion(), "Missing service token", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string jwtToken = ctx->request->getHeader("x-nintendo-service-token")[0];

    if (!crypto::verifyJWT(settingsManager->getNEXTokenKey(), jwtToken)) {
        res = createAccSettingsError(ctx->request->getVersion(), "Invalid service token", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string jwtData = jwtToken.substr(jwtToken.find('.') + 1);
    jwtData = jwtData.substr(0, jwtData.find('.'));

    auto jwtJson = nlohmann::json::parse(crypto::base64UrlDecode(jwtData));
    if (time(nullptr) > jwtJson["exp"].get<time_t>() || jwtJson["iss"].get<std::string>() != "account"
        || !jwtJson.contains("clientId") || jwtJson["clientId"].get<std::string>() != "3f3928cc6f780638d360f0485cef973f") {
        res = createAccSettingsError(ctx->request->getVersion(), "Invalid service token", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    uint32_t pid = jwtJson["sub"].get<uint32_t>();

    // Get user info
    auto getUserCmd = db::Database::craftGetUserProfileCommand(pid);
    auto getUserResults = co_await db->runCommand(std::move(getUserCmd));
    if (getUserResults.getStatus() != db::DBResultStatus::SUCCESS) {
        res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!getUserResults.hasData() || !getUserResults.getData<db::DBUserProfileData>().active) {
        res = createAccSettingsError(ctx->request->getVersion(), "User not found", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    auto userProfile = getUserResults.getData<db::DBUserProfileData>();

    json data;
    try {
        data = nlohmann::json::parse(ctx->request->getBody());
    } catch (const std::exception&) {
        res = createAccSettingsError(ctx->request->getVersion(), "Invalid JSON body", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!data.contains("action")) {
        res = createAccSettingsError(ctx->request->getVersion(), "Missing action", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }

    if (data["action"] == "save_personal_info") {
        std::optional<bool> gender = std::nullopt;
        if (data.contains("gender")) {
            std::string genderStr = data["gender"];
            if (genderStr != "male" && genderStr != "female") {
                res = createAccSettingsError(ctx->request->getVersion(), "Bad request body", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }

            gender = genderStr == "female";
        }

        std::optional<int64_t> region = std::nullopt;
        if (data.contains("region")) {
            std::string regionStr = data["region"];
            try {
                region = std::stoll(regionStr);
            } catch (const std::invalid_argument&) {
                res = createAccSettingsError(ctx->request->getVersion(), "Bad request body", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            } catch (const std::out_of_range&) {
                res = createAccSettingsError(ctx->request->getVersion(), "Bad request body", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
        }

        std::optional<std::string> timezone = std::nullopt;
        if (data.contains("timezone")) {
            bool found = false;
            if (timezones.contains(userProfile.country)) {
                std::string languageToCheck = userProfile.language.empty() ? "en" : userProfile.language;
                if (timezones[userProfile.country].contains(languageToCheck)) {
                    for (const auto& tz : timezones[userProfile.country][languageToCheck]) {
                        if (tz["area"].get<std::string>() == data["timezone"].get<std::string>()) {
                            found = true;
                            timezone = tz["area"].get<std::string>();
                            break;
                        }
                    }
                }
            }

            if (!found) {
                res = createAccSettingsError(ctx->request->getVersion(), "Bad request body", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
        }

        auto updateCmd = db::Database::craftUpdateUserProfileCommand(pid,
            std::nullopt, // username
            std::nullopt, // password
            std::nullopt, // email
            std::nullopt, // mii
            gender,
            region,
            timezone,
            std::nullopt, // language
            std::nullopt, // active
            std::nullopt, // marketing
            std::nullopt, // offDevice
            std::nullopt, // birthdate
            std::nullopt, // country
            std::nullopt, // created
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated
        auto updateResults = co_await db->runCommand(std::move(updateCmd));
        if (updateResults.getStatus() != db::DBResultStatus::SUCCESS) {
            res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    } else if (data["action"] == "save_notifications") {
        if (data.contains("notifications")) {
            std::optional marketing = data["notifications"] == "enabled";
            auto updateCmd = db::Database::craftUpdateUserProfileCommand(pid,
            std::nullopt, // username
            std::nullopt, // password
            std::nullopt, // email
            std::nullopt, // mii
            std::nullopt, // gender
            std::nullopt, // region
            std::nullopt, // tz
            std::nullopt, // language
            std::nullopt, // active
            marketing,
            std::nullopt, // offDevice
            std::nullopt, // birthdate
            std::nullopt, // country
            std::nullopt, // created
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated
            auto updateResults = co_await db->runCommand(std::move(updateCmd));
            if (updateResults.getStatus() != db::DBResultStatus::SUCCESS) {
                res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
        }
    } else if (data["action"] == "save_device_access") {
        if (data.contains("deviceAccess")) {
            std::optional offDevice = data["deviceAccess"] == "enabled";
            auto updateCmd = db::Database::craftUpdateUserProfileCommand(pid,
            std::nullopt, // username
            std::nullopt, // password
            std::nullopt, // email
            std::nullopt, // mii
            std::nullopt, // gender
            std::nullopt, // region
            std::nullopt, // tz
            std::nullopt, // language
            std::nullopt, // active
            std::nullopt, // marketing
            offDevice,
            std::nullopt, // birthdate
            std::nullopt, // country
            std::nullopt, // created
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated
            auto updateResults = co_await db->runCommand(std::move(updateCmd));
            if (updateResults.getStatus() != db::DBResultStatus::SUCCESS) {
                res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
        }
    } else if (data["action"] == "save_email") {
        if (data.contains("newEmail")) {
            if (!checkEmailAddress(data["newEmail"].get<std::string>())) {
                res = createAccSettingsError(ctx->request->getVersion(), "Invalid email address", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }

            auto session = db->createSession();
            if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
                res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }

            std::string validationCode = crypto::genRandomString(6, "0123456789");

            auto emailCmd = db::Database::craftInsertOrUpdateEmailCommand(
                std::nullopt, // emailId
                data["newEmail"].get<std::string>(),
                "N",
                "Y",
                true, // reachable
                "DEFAULT",
                "USER",
                !settingsManager->isAccountsEmailEnabled(),
                db::datetime_t(),
                validationCode);
            
            auto emailResults = co_await db->runCommand(std::move(emailCmd));
            if (emailResults.getStatus() != db::DBResultStatus::SUCCESS) {
                res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
            
            int64_t oldEmailId = userProfile.emailId;
            
            auto updateCmd = db::Database::craftUpdateUserProfileCommand(pid,
            std::nullopt, // username
            std::nullopt, // password
            emailResults.getData<int64_t>(),
            std::nullopt, // mii
            std::nullopt, // gender
            std::nullopt, // region
            std::nullopt, // tz
            std::nullopt, // language
            std::nullopt, // active
            std::nullopt, // marketing
            std::nullopt, // offDevice
            std::nullopt, // birthdate
            std::nullopt, // country
            std::nullopt, // created
            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())); // updated
            auto updateResults = co_await db->runCommand(std::move(updateCmd));
            if (updateResults.getStatus() != db::DBResultStatus::SUCCESS) {
                res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
            
            std::unique_ptr<db::Command> deleteEmailCmd = db::Database::craftDeleteEmailCommand(oldEmailId);
            if ((co_await session->runCommand(std::move(deleteEmailCmd))).getStatus() != db::DBResultStatus::SUCCESS) {
                co_await session->rollbackTransaction();
                res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }

            if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
                res = createAccSettingsError(ctx->request->getVersion(), "Database error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
            
            mailio::message msg;
            msg.add_recipient(mailio::mail_address("", data["newEmail"].get<std::string>()));
            msg.subject("SplatIt Email Updated");
            msg.content("Hello, " + userProfile.username + "!\r\n\r\n"
                        "Your SplatIt account email has been updated successfully!\r\n"
                        "The code to validate your email is: " + validationCode + "\r\n\r\n"
                        "Enter the code in the console in order to validate your email. Link validation is not available yet.\r\n\r\n"
                        "If you don't have an SplatIt account, please ignore this email.\r\n\r\n"
                        "Thank you for using SplatIt!");

            ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                "Sending email update email to " + data["newEmail"].get<std::string>() + " for user " +
                userProfile.username + ".");
            if (!sendEmail(ctx->logger, settingsManager, msg)) {
                res = createAccSettingsError(ctx->request->getVersion(), "Failed to send email. Account was successfully updated.", HTTP_STATUS_BAD_REQUEST);
                srv->sendResponse(std::move(ctx), std::move(res), false);
                co_return;
            }
        }
    } else {
        res = createAccSettingsError(ctx->request->getVersion(), "Unknown action", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
    }
    
    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

} // namespace acc

