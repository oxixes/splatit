#ifndef SPLATOON_SERVER_ACCOUNT_HPP
#define SPLATOON_SERVER_ACCOUNT_HPP

#include <memory>

#include <pugixml.hpp>

#include "../parser/response.hpp"
#include "../../db/database.hpp"
#include "../parser/request.hpp"
#include "../server.hpp"
#include "../../settingsManager.hpp"
#include "../../crypto/certManager.hpp"

namespace crypto {
    struct AccountToken;
}

namespace acc {

void v1_api_admin_time(http::Server* srv, std::shared_ptr<http::Context> ctx);
void v1_api_admin_mapped_ids(http::Server* srv, std::shared_ptr<http::Context> ctx,
                             const std::shared_ptr<db::Database>& db,
                             const std::shared_ptr<SettingsManager>& settingsManager,
                             const std::shared_ptr<CertManager>& certManager);

void v1_api_access_token_gen(http::Server* srv, std::shared_ptr<http::Context> ctx,
                             const std::shared_ptr<db::Database>& db,
                             const std::shared_ptr<SettingsManager>& settingsManager,
                             const std::shared_ptr<CertManager>& certManager);

void v1_api_provider_nex_token(http::Server* srv, std::shared_ptr<http::Context> ctx,
                               const std::shared_ptr<db::Database>& db,
                               const std::shared_ptr<SettingsManager>& settingsManager,
                               const std::shared_ptr<CertManager>& certManager);

void v1_api_people_me_profile(http::Server* srv, std::shared_ptr<http::Context> ctx,
                              const std::shared_ptr<db::Database>& db,
                              const std::shared_ptr<SettingsManager>& settingsManager,
                              const std::shared_ptr<CertManager>& certManager);

void v1_api_provider_service_token_me(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                      const std::shared_ptr<db::Database>& db,
                                      const std::shared_ptr<SettingsManager>& settingsManager,
                                      const std::shared_ptr<CertManager>& certManager);

void v1_api_content_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx,
                               const std::string& type, const std::string& country, const std::string& version,
                               const std::shared_ptr<db::Database>& db,
                               const std::shared_ptr<SettingsManager>& settingsManager,
                               const std::shared_ptr<CertManager>& certManager);

void v1_api_content_timezones(http::Server* srv, std::shared_ptr<http::Context> ctx,
                              const std::string& country, const std::string& language,
                              const std::shared_ptr<SettingsManager>& settingsManager,
                              const std::shared_ptr<CertManager>& certManager);

std::unique_ptr<http::Response> createError(http::Version version, int code, const std::string& message, const std::string& cause, int httpStatus);

void errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx);

std::unique_ptr<http::Response> prepareResponse(http::Version version, int httpStatus = HTTP_STATUS_OK);
std::unique_ptr<http::Response> prepareResponse(http::Version version, pugi::xml_document& doc, int httpStatus = HTTP_STATUS_OK);

bool checkDeviceCert(const std::string& cert, EVP_PKEY* pubKey);
bool checkOauthToken(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                     crypto::AccountToken& token);

bool checkRequestParams(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<CertManager>& certManager, std::unique_ptr<http::Response>& resOut,
                        bool checkDevice = true);

bool init(const std::shared_ptr<Logger::Logger>& logger);

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<CertManager> certMgr, std::shared_ptr<db::Database> db);

} // namespace acc

#endif //SPLATOON_SERVER_ACCOUNT_HPP
