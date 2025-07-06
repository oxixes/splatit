#ifndef SPLATOON_SERVER_ACCOUNT_HPP
#define SPLATOON_SERVER_ACCOUNT_HPP

#include <memory>

#include <pugixml.hpp>

#include "../parser/response.hpp"
#include "../../logger.hpp"
#include "../../db/database.hpp"
#include "../parser/request.hpp"
#include "../../socket/socket.hpp"
#include "../server.hpp"
#include "../../settingsManager.hpp"
#include "../../crypto/certManager.hpp"

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

std::unique_ptr<http::Response> createError(http::Version version, int code, const std::string& message, const std::string& cause);

void errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx);

std::unique_ptr<http::Response> prepareResponse(http::Version version);
std::unique_ptr<http::Response> prepareResponse(http::Version version, pugi::xml_document& doc);

bool checkDeviceCert(const std::string& cert, EVP_PKEY* pubKey);

bool checkRequestParams(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<CertManager>& certManager, std::unique_ptr<http::Response>& resOut,
                        bool checkDevice = true);

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<CertManager> certMgr, std::shared_ptr<db::Database> db);

} // namespace acc

#endif //SPLATOON_SERVER_ACCOUNT_HPP
