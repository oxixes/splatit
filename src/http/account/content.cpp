#include "account.hpp"

#include <date/tz.h>

namespace acc {

using namespace async;

/*
 * Handler for GET https://account.<domain>/v1/api/content/agreements/<type>/<country>/<version>
 * Obtains the EULA for the given type and country and version.
 */
Task<void> v1_api_content_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                     const std::string& type, const std::string& country, const std::string& version,
                                     const std::shared_ptr<db::Database>& db,
                                     const std::shared_ptr<SettingsManager>& settingsManager,
                                     const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res = std::make_unique<http::Response>(ctx->request->getVersion(), HTTP_STATUS_OK);
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::string language = "en";
    if (ctx->request->hasHeader("accept-language")) {
        language = ctx->request->getHeader("accept-language")[0];
    }

    std::optional<int> versionInt = std::nullopt;
    if (version != "@latest") {
        // Check if version is a valid integer
        try {
            versionInt = std::stoi(version);
        } catch (const std::invalid_argument&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid version", "version", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        } catch (const std::out_of_range&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid version", "version", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    int blockLength = 2048;
    if (ctx->request->hasQuery("length")) {
        try {
            blockLength = std::stoi(ctx->request->getQuery("length"));
        } catch (const std::invalid_argument&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid length", "length", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        } catch (const std::out_of_range&) {
            res = createError(ctx->request->getVersion(), 1101, "Invalid length", "length", HTTP_STATUS_BAD_REQUEST);
            srv->sendResponse(std::move(ctx), std::move(res), false);
            co_return;
        }
    }

    std::unique_ptr<db::Command> cmd = db::Database::craftGetAgreementCommand(type, country, language, versionInt);
    db::Result results = co_await db->runCommand(ctx->scheduler, std::move(cmd));

    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        ctx->logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Database error while getting agreement");
        res = createError(ctx->request->getVersion(), 2001, "Unable to process request", "Internal Server Error", HTTP_STATUS_INTERNAL_SERVER_ERROR);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    // Build the XML document
    pugi::xml_document doc;
    pugi::xml_node agreements = doc.append_child("agreements");
    pugi::xml_node agreement = agreements.append_child("agreement");
    agreement.append_child("country").text().set(country.c_str(), country.length());
    agreement.append_child("language").text().set(language.c_str(), language.length());
    agreement.append_child("type").text().set(type.c_str(), type.length());

    pugi::xml_node texts = agreement.append_child("texts");
    texts.append_attribute("xmlns:xsi") = "http://www.w3.org/2001/XMLSchema-instance";
    texts.append_attribute("xsi:type") = "chunkedStoredAgreementText";

    if (!results.hasData()) {
        // Create a dummy agreement if none was found
        ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT,
                         "No agreement found for type " + type + ", country " + country + ", language " + language + ", version " + version);

        std::string versionStr = "0001";
        if (versionInt.has_value()) {
            std::ostringstream oss;
            oss << std::setw(4) << std::setfill('0') << versionInt.value();
            versionStr = oss.str();
        }

        agreement.append_child("version").text().set(versionStr.c_str(), versionStr.length());
        std::string currentDate = util::getDateISO8601(std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));
        agreement.append_child("publish_date").text().set(currentDate.c_str(), currentDate.length());
        agreement.append_child("language_name").text().set("Unknown");

        texts.append_child("agree_text").append_child(pugi::node_cdata).set_value("Agree");
        texts.append_child("non_agree_text").append_child(pugi::node_cdata).set_value("Disagree");
        texts.append_child("main_title").append_child(pugi::node_cdata).set_value("SplatIt Contract");
        texts.append_child("sub_title").append_child(pugi::node_cdata).set_value("SplatIt Privacy Policy");

        std::string dummyText = "Hey, if you are reading this, it means the server administrator has not set up "
                                "the agreements for your country and/or language. Please contact them to fix this.\r\n";

        // Split the text into chunks of blockLength bytes
        int chunkIdx = 1;
        for (size_t i = 0; i < dummyText.length(); i += blockLength) {
            std::string chunk = dummyText.substr(i, blockLength);
            pugi::xml_node mainChunk = texts.append_child("main_text");
            mainChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
            mainChunk.append_attribute("index") = chunkIdx;

            pugi::xml_node subTextChunk = texts.append_child("sub_text");
            subTextChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
            subTextChunk.append_attribute("index") = chunkIdx++;
        }
    } else {
        auto agreementData = std::move(results.getData<db::DBAgreementData>());
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << agreementData.version;
        std::string versionStr = oss.str();

        agreement.append_child("version").text().set(versionStr.c_str(), versionStr.length());
        std::string publishDate = util::getDateISO8601(agreementData.publishedAt);
        agreement.append_child("publish_date").text().set(publishDate.c_str(), publishDate.length());
        agreement.append_child("language_name").text().set(agreementData.languageName.c_str(), agreementData.languageName.length());
        texts.append_child("agree_text").append_child(pugi::node_cdata).set_value(agreementData.agreeText.c_str(), agreementData.agreeText.length());
        texts.append_child("non_agree_text").append_child(pugi::node_cdata).set_value(agreementData.disagreeText.c_str(), agreementData.disagreeText.length());
        texts.append_child("main_title").append_child(pugi::node_cdata).set_value(agreementData.mainTitle.c_str(), agreementData.mainTitle.length());
        texts.append_child("sub_title").append_child(pugi::node_cdata).set_value(agreementData.subTitle.c_str(), agreementData.subTitle.length());

        size_t pos = 0;
        while ((pos = agreementData.mainText.find("\r\n", pos)) != std::string::npos) {
            agreementData.mainText.replace(pos, 2, "\n");
            pos += 1;
        }

        for (auto& c : agreementData.mainText) {
            if (c == '\r') c = '\n';
        }

        std::string mainTextResult;
        mainTextResult.reserve(agreementData.mainText.size());

        for (size_t i = 0; i < agreementData.mainText.size(); ++i) {
            if (agreementData.mainText[i] == '\n') {
                if (i == 0 || agreementData.mainText[i - 1] != '\r') {
                    mainTextResult += '\r';
                }
            }

            mainTextResult += agreementData.mainText[i];
        }

        pos = 0;
        while ((pos = agreementData.subText.find("\r\n", pos)) != std::string::npos) {
            agreementData.subText.replace(pos, 2, "\n");
            pos += 1;
        }

        for (auto& c : agreementData.subText) {
            if (c == '\r') c = '\n';
        }

        std::string subTextResult;
        subTextResult.reserve(agreementData.subText.size());
        for (size_t i = 0; i < agreementData.subText.size(); ++i) {
            if (agreementData.subText[i] == '\n') {
                if (i == 0 || agreementData.subText[i - 1] != '\r') {
                    subTextResult += '\r';
                }
            }

            subTextResult += agreementData.subText[i];
        }

        if (mainTextResult.at(mainTextResult.size() - 1) != '\n') {
            mainTextResult += "\r\n"; // Ensure the last line ends with a newline
        }

        if (subTextResult.at(subTextResult.size() - 1) != '\n') {
            subTextResult += "\r\n"; // Ensure the last line ends with a newline
        }

        int chunkIdx = 1;
        for (int i = 0; i < mainTextResult.size(); i += blockLength) {
            std::string chunk = mainTextResult.substr(i, blockLength);
            pugi::xml_node mainChunk = texts.append_child("main_text");
            mainChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
            mainChunk.append_attribute("index") = chunkIdx++;
        }

        chunkIdx = 1; // Reset chunk index for sub text
        for (int i = 0; i < subTextResult.size(); i += blockLength) {
            std::string chunk = subTextResult.substr(i, blockLength);
            pugi::xml_node subTextChunk = texts.append_child("sub_text");
            subTextChunk.append_child(pugi::node_cdata).set_value(chunk.c_str(), chunk.length());
            subTextChunk.append_attribute("index") = chunkIdx++;
        }
    }

    res = prepareResponse(ctx->request->getVersion(), doc);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

/*
 * Handler for GET https://account.<domain>/v1/api/content/time_zones/<country>/<language>
 * Obtains the available time zones for the given country in the specified language.
 */
Task<void> v1_api_content_timezones(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                   const std::string& country, const std::string& language,
                                   const std::shared_ptr<SettingsManager>& settingsManager,
                                   const std::shared_ptr<CertManager>& certManager) {
    if (ctx->request->getMethod() != http::Method::M_GET) {
        std::unique_ptr<http::Response> res = createError(ctx->request->getVersion(), 9, "Method Not Allowed", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    std::unique_ptr<http::Response> res;
    if (!checkRequestParams(ctx->request, settingsManager, certManager, res, false)) {
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    if (!timezones.contains(country) || !timezones[country].contains(language)) {
        res = createError(ctx->request->getVersion(), 8, "Not Found", "", HTTP_STATUS_NOT_FOUND);
        srv->sendResponse(std::move(ctx), std::move(res), false);
        co_return;
    }

    pugi::xml_document doc;
    pugi::xml_node timezonesNode = doc.append_child("timezones");
    int order = 1;
    for (const auto& timezone : timezones[country][language]) {
        std::string area = timezone["area"].get<std::string>();
        std::string name = timezone["name"].get<std::string>();

        // ATTENTION Code explorer!
        // You have stumbled upon a time and timezone related section of the code!
        // These sections are probably one of the worst a developer can deal with.
        // The code below uses the date library to get the current offset. It CAN'T be stored
        // as a static value, because the offset can change due to daylight saving time and the
        // real server takes this into account.

        auto tz = date::locate_zone(area);
        if (!tz) {
            ctx->logger->log(Logger::level::WARN, Logger::group::ACCOUNT,
                             "Invalid timezone area: " + area + " for country: " + country + ", language: " + language);
            continue;
        }

        auto now = std::chrono::system_clock::now();
        date::zoned_time zt{tz, now};
        auto info = zt.get_info();

        int64_t utcOffset = std::chrono::duration_cast<std::chrono::seconds>(info.offset).count();

        pugi::xml_node timezoneNode = timezonesNode.append_child("timezone");

        timezoneNode.append_child("area").text().set(area.c_str(), area.length());
        timezoneNode.append_child("name").text().set(name.c_str(), name.length());
        timezoneNode.append_child("utc_offset").text().set(std::to_string(utcOffset).c_str(), std::to_string(utcOffset).length());
        timezoneNode.append_child("order").text().set(std::to_string(order).c_str(), std::to_string(order).length());
        timezoneNode.append_child("language").text().set(language.c_str(), language.length());

        order++;
    }

    res = prepareResponse(ctx->request->getVersion(), doc, HTTP_STATUS_OK);
    srv->sendResponse(std::move(ctx), std::move(res), false);
}

} // namespace acc
