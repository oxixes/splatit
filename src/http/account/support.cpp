#include "account.hpp"

#include "../../crypto/tools.hpp"

namespace acc {

using namespace async;

/*
 * Handler for POST https://account.<domain>/v1/api/support/validate/email
 * Validates an email address to be used for account creation.
 */
Task<void> v1_api_support_validate_email(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                         std::shared_ptr<db::Database> db,
                                         std::shared_ptr<SettingsManager> settingsManager,
                                         std::shared_ptr<CertManager> certManager) {
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

    if (!ctx->request->hasHeader("content-type") || ctx->request->getHeader("content-type")[0] != "application/x-www-form-urlencoded") {
        res = createError(ctx->request->getVersion(), 1600, "Unable to process request", "Unsupported Media Type", HTTP_STATUS_BAD_REQUEST);
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

    if (!bodyMap.contains("email") || !checkEmailAddress(bodyMap["email"])) {
        res = createError(ctx->request->getVersion(), 103, "Email format is invalid", "email", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for PUT https://account.<domain>/v1/api/support/email_confirmation/<pid>/<validation_code>
 * Validates an email address given the principal ID and validation code.
 */
Task<void> v1_api_support_email_confirmation(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                             std::string pid, std::string validationCode,
                                             std::shared_ptr<db::Database> db,
                                             std::shared_ptr<SettingsManager> settingsManager,
                                             std::shared_ptr<CertManager> certManager) {
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

    uint32_t pidNum;
    try {
        pidNum = std::stoul(pid);
    } catch (const std::invalid_argument&) {
        res = createError(ctx->request->getVersion(), 1, "Invalid principal ID", "pid", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    } catch (const std::out_of_range&) {
        res = createError(ctx->request->getVersion(), 1, "Invalid principal ID", "pid", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto profileCmd = db::Database::craftGetUserProfileCommand(pidNum);
    auto profileRes = co_await db->runCommand(std::move(profileCmd));
    if (profileRes.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");
    if (!profileRes.hasData()) {
        res = createError(ctx->request->getVersion(), 130, "Principal ID not found", "pid", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto profileData = std::move(profileRes.getData<db::DBUserProfileData>());
    if (profileData.emailValidated) {
        res = createError(ctx->request->getVersion(), 7, "Email already validated", "", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (profileData.emailValidationCode != validationCode) {
        res = createError(ctx->request->getVersion(), 116, "Invalid validation code", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    db::datetime_t validatedAt = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    auto updateCmd = db::Database::craftInsertOrUpdateEmailCommand(
        profileData.emailId,
        profileData.email,
        profileData.emailParent,
        profileData.emailPrimary,
        true, // Set email as reachable
        profileData.emailType,
        "USER",
        true, // Set email as validated
        validatedAt,
        "000000");
    auto updateRes = co_await db->runCommand(std::move(updateCmd));
    if (updateRes.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

    ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                     "User " + profileData.username + " (with PID " + std::to_string(pidNum) + ") validated their email address.");

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/support/forgotten_password/<pid>
 * Sends a forgotten password email to the user with the given principal ID.
 */
Task<void> v1_api_support_forgotten_password(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                              std::string pid, std::shared_ptr<db::Database> db,
                                              std::shared_ptr<SettingsManager> settingsManager,
                                              std::shared_ptr<CertManager> certManager) {
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

    uint32_t pidNum;
    try {
        pidNum = std::stoul(pid);
    } catch (const std::invalid_argument&) {
        res = createError(ctx->request->getVersion(), 1, "Invalid principal ID", "pid", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    } catch (const std::out_of_range&) {
        res = createError(ctx->request->getVersion(), 1, "Invalid principal ID", "pid", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    auto profileCmd = db::Database::craftGetUserProfileCommand(pidNum);
    auto profileRes = co_await session->runCommand(std::move(profileCmd));
    if (profileRes.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        throw std::runtime_error("Database error");
    }

    if (!profileRes.hasData()) {
        res = createError(ctx->request->getVersion(), 130, "Principal ID not found", "pid", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto profileData = std::move(profileRes.getData<db::DBUserProfileData>());
    if (!profileData.emailValidated) {
        co_await session->rollbackTransaction();
        res = createError(ctx->request->getVersion(), 128, "Email has not been validated yet", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string newPassword = crypto::genRandomString(16, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+[]{}|;:,.<>?/");
    std::string hashedPassword;
    bool error = false;
    try {
        std::string nintendoPasswordHash = crypto::genNintendoPasswordHash(pidNum, newPassword);
        std::string salt = crypto::genSalt();
        hashedPassword = crypto::hashPassword(nintendoPasswordHash, salt);
    } catch (const std::exception& e) {
        error = true;
    }

    if (error) {
        co_await session->rollbackTransaction();
        res = createError(ctx->request->getVersion(), 2001, "Internal server error", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    db::datetime_t lastUpdated = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    auto updateCmd = db::Database::craftUpdateUserProfileCommand(
        pidNum,
        std::nullopt, // username
        hashedPassword,
        std::nullopt, // email
        std::nullopt, // miiId
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
        lastUpdated); // updated
    auto updateRes = co_await session->runCommand(std::move(updateCmd));
    if (updateRes.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        throw std::runtime_error("Database error");
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    mailio::message msg;
    msg.add_recipient(mailio::mail_address("", profileData.email));
    msg.subject("SplatIt Password Recovery");
    msg.content("Hello, " + profileData.username + "!\r\n\r\n"
                "You requested a password recovery, so we're sending you a new password. Use it to login and change it to the one you want.\r\n"
                "The new password is: " + newPassword + "\r\n\r\n"
                "Thank you for using SplatIt!");

    ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
        "Sending password recovery email to " + profileData.email + " for user " + profileData.username + ".");
    if (!sendEmail(ctx->logger, settingsManager, msg)) {
        res = createError(ctx->request->getVersion(), 1031, "Failed to send email", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/support/resend_confirmation
 * Resends the email confirmation for the user with the given principal ID.
 */
Task<void> v1_api_support_resend_confirmation(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                              std::shared_ptr<db::Database> db,
                                              std::shared_ptr<SettingsManager> settingsManager,
                                              std::shared_ptr<CertManager> certManager) {
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

    if (!ctx->request->hasHeader("x-nintendo-pid")) {
        res = createError(ctx->request->getVersion(), 2, "X-Nintendo-PID format is invalid", "X-Nintendo-PID", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string pidStr = ctx->request->getHeader("x-nintendo-pid")[0];
    uint32_t pid;
    try {
        pid = std::stoul(pidStr);
    } catch (const std::invalid_argument&) {
        res = createError(ctx->request->getVersion(), 1, "X-Nintendo-PID format is invalid", "X-Nintendo-PID", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    } catch (const std::out_of_range&) {
        res = createError(ctx->request->getVersion(), 1, "X-Nintendo-PID format is invalid", "X-Nintendo-PID", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto profileCmd = db::Database::craftGetUserProfileCommand(pid);
    auto profileRes = co_await db->runCommand(std::move(profileCmd));
    if (profileRes.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");
    if (!profileRes.hasData()) {
        res = createError(ctx->request->getVersion(), 130, "Principal ID not found", "X-Nintendo-PID", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    auto profileData = std::move(profileRes.getData<db::DBUserProfileData>());
    if (profileData.emailValidated) {
        res = createError(ctx->request->getVersion(), 7, "Email already validated", "", HTTP_STATUS_FORBIDDEN);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string validationCode = crypto::genRandomString(6, "0123456789");
    auto updateCmd = db::Database::craftInsertOrUpdateEmailCommand(
        profileData.emailId,
        profileData.email,
        profileData.emailParent,
        profileData.emailPrimary,
        profileData.emailReachable,
        profileData.emailType,
        "USER",
        false, // Set email as not validated
        db::datetime_t(), // No validation date yet
        validationCode);
    auto updateRes = co_await db->runCommand(std::move(updateCmd));
    if (updateRes.getStatus() != db::DBResultStatus::SUCCESS) throw std::runtime_error("Database error");

    mailio::message msg;
    msg.add_recipient(mailio::mail_address("", profileData.email));
    msg.subject("SplatIt Email Confirmation");
    msg.content("Hello, " + profileData.username + "!\r\n\r\n"
                "You requested a new confirmation code.\r\n"
                "The code to validate your email is: " + validationCode + "\r\n\r\n"
                "Enter the code in the console in order to validate your email. Link validation is not available yet.\r\n\r\n"
                "If you don't have an SplatIt account, please ignore this email.\r\n\r\n"
                "Thank you for using SplatIt!");

    ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
        "Sending email confirmation email to " + profileData.email + " for user " + profileData.username + ".");
    if (!sendEmail(ctx->logger, settingsManager, msg)) {
        res = createError(ctx->request->getVersion(), 1031, "Failed to send email", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/support/send_confirmation/pin/<email>
 * Sends an email confirming the user that the email address has been set for parental controls recovery.
 */
Task<void> v1_api_support_send_confirmation_pin(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                  std::string email, std::shared_ptr<db::Database> db,
                                                  std::shared_ptr<SettingsManager> settingsManager,
                                                  std::shared_ptr<CertManager> certManager) {
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

    std::string decodedEmail;
    try {
        decodedEmail = http::percentDecode(email);
    } catch (const std::exception&) {
        res = createError(ctx->request->getVersion(), 2, "Malformed request", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!checkEmailAddress(decodedEmail)) {
        res = createError(ctx->request->getVersion(), 103, "Email format is invalid", "email", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    mailio::message msg;
    msg.add_recipient(mailio::mail_address("", email));
    msg.subject("SplatIt Parental Controls Confirmation");
    msg.content("Your email address has been set successfully for parental controls recovery.\r\n\r\n"
                "Thank you for using SplatIt!");

    ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                     "Sending parental controls confirmation email to " + email + ".");
    if (!sendEmail(ctx->logger, settingsManager, msg)) {
        res = createError(ctx->request->getVersion(), 1031, "Failed to send email", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/support/send_forgotten/pin/<email>/<pin>
 * Sends the forgotten parental controls PIN to the email address.
 */
Task<void> v1_api_support_send_forgotten_pin(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                             std::string email, std::string pin, std::shared_ptr<db::Database> db,
                                             std::shared_ptr<SettingsManager> settingsManager,
                                             std::shared_ptr<CertManager> certManager) {
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

    std::string decodedEmail;
    try {
        decodedEmail = http::percentDecode(email);
    } catch (const std::exception&) {
        res = createError(ctx->request->getVersion(), 2, "Malformed request", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!checkEmailAddress(decodedEmail)) {
        res = createError(ctx->request->getVersion(), 103, "Email format is invalid", "email", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    mailio::message msg;
    msg.add_recipient(mailio::mail_address("", decodedEmail));
    msg.subject("SplatIt Parental Controls Forgotten Code");
    msg.content("You requested a forgotten parental controls code.\r\n\r\n"
                "Your parental controls code is: " + pin + "\r\n\r\n"
                "You can input this code in the parental controls settings to recover access to the configuration.\r\n\r\n"
                "Thank you for using SplatIt!");

    ctx->logger->log(Logger::level::INFO, Logger::group::ACCOUNT,
                     "Sending forgotten parental controls PIN email to " + decodedEmail + ".");
    if (!sendEmail(ctx->logger, settingsManager, msg)) {
        res = createError(ctx->request->getVersion(), 1031, "Failed to send email", "", HTTP_STATUS_BAD_REQUEST);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    res = prepareResponse(ctx->request->getVersion(), HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

} // namespace acc