#ifndef SPLATOON_SERVER_ACCOUNT_HPP
#define SPLATOON_SERVER_ACCOUNT_HPP

#include <memory>
#include <pugixml.hpp>
#include <mailio/mailboxes.hpp>
#include <mailio/message.hpp>
#include <nlohmann/json.hpp>

#include "../parser/response.hpp"
#include "../../db/database.hpp"
#include "../parser/request.hpp"
#include "../server.hpp"
#include "../../settingsManager.hpp"
#include "../../crypto/certManager.hpp"
#include "../../grpc/channelPool.hpp"
#include "../../util/task.hpp"

namespace crypto {
    struct AccountToken;
}

namespace acc {

extern std::shared_ptr<grpcimpl::ChannelPool> channelPool;
extern std::map<std::string, std::vector<std::pair<std::string, std::string>>> gameServerHosts;
extern std::map<std::string, size_t> gameServerHostIndexRoundRobin;
extern json timezones;

async::Task<void> v1_api_admin_time(const http::Server* srv, std::shared_ptr<http::Context> ctx);
async::Task<void> v1_api_admin_mapped_ids(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                          std::shared_ptr<db::Database> db,
                                          std::shared_ptr<SettingsManager> settingsManager,
                                          std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_access_token_gen(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                          std::shared_ptr<db::Database> db,
                                          std::shared_ptr<SettingsManager> settingsManager,
                                          std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_provider_nex_token(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                            std::shared_ptr<db::Database> db,
                                            std::shared_ptr<SettingsManager> settingsManager,
                                            std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_devices_current_status(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                std::shared_ptr<db::Database> db,
                                                std::shared_ptr<SettingsManager> settingsManager,
                                                std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_devices_current_inactivate(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                    std::shared_ptr<db::Database> db,
                                                    std::shared_ptr<SettingsManager> settingsManager,
                                                    std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_nnid(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                     std::string nnid,
                                     std::shared_ptr<db::Database> db,
                                     std::shared_ptr<SettingsManager> settingsManager,
                                     std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                std::shared_ptr<db::Database> db,
                                std::shared_ptr<SettingsManager> settingsManager,
                                std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                   std::shared_ptr<db::Database> db,
                                   std::shared_ptr<SettingsManager> settingsManager,
                                   std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_emails(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                          std::shared_ptr<db::Database> db,
                                          std::shared_ptr<SettingsManager> settingsManager,
                                          std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_miis_primary(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                std::shared_ptr<db::Database> db,
                                                std::shared_ptr<SettingsManager> settingsManager,
                                                std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_devices_current_attributes(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                              std::shared_ptr<db::Database> db,
                                                              std::shared_ptr<SettingsManager> settingsManager,
                                                              std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                              std::shared_ptr<db::Database> db,
                                              std::shared_ptr<SettingsManager> settingsManager,
                                              std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_profile(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                           std::shared_ptr<db::Database> db,
                                           std::shared_ptr<SettingsManager> settingsManager,
                                           std::shared_ptr<CertManager> certManager,
                                           std::optional<uint32_t> pid);

async::Task<void> v1_api_people_me_devices_owner(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                 std::shared_ptr<db::Database> db,
                                                 std::shared_ptr<SettingsManager> settingsManager,
                                                 std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_devices_get(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                               std::shared_ptr<db::Database> db,
                                               std::shared_ptr<SettingsManager> settingsManager,
                                               std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_devices_post(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                std::shared_ptr<db::Database> db,
                                                std::shared_ptr<SettingsManager> settingsManager,
                                                std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_devices_current_inactivate(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                            std::shared_ptr<db::Database> db,
                                                            std::shared_ptr<SettingsManager> settingsManager,
                                                            std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_people_me_deletion(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                            std::shared_ptr<db::Database> db,
                                            std::shared_ptr<SettingsManager> settingsManager,
                                            std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_provider_service_token_me(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                   std::shared_ptr<db::Database> db,
                                                   std::shared_ptr<SettingsManager> settingsManager,
                                                   std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_content_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                            std::string type, std::string country, std::string version,
                                            std::shared_ptr<db::Database> db,
                                            std::shared_ptr<SettingsManager> settingsManager,
                                            std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_content_timezones(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                           std::string country, std::string language,
                                           std::shared_ptr<SettingsManager> settingsManager,
                                           std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_miis(http::Server* srv, std::shared_ptr<http::Context> ctx,
                              std::shared_ptr<db::Database> db,
                              std::shared_ptr<SettingsManager> settingsManager,
                              std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_support_validate_email(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                std::shared_ptr<db::Database> db,
                                                std::shared_ptr<SettingsManager> settingsManager,
                                                std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_support_email_confirmation(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                    std::string pid, std::string validationCode,
                                                    std::shared_ptr<db::Database> db,
                                                    std::shared_ptr<SettingsManager> settingsManager,
                                                    std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_support_forgotten_password(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                    std::string pid, std::shared_ptr<db::Database> db,
                                                    std::shared_ptr<SettingsManager> settingsManager,
                                                    std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_support_resend_confirmation(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                     std::shared_ptr<db::Database> db,
                                                     std::shared_ptr<SettingsManager> settingsManager,
                                                     std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_support_send_confirmation_pin(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                       std::string email, std::shared_ptr<db::Database> db,
                                                       std::shared_ptr<SettingsManager> settingsManager,
                                                       std::shared_ptr<CertManager> certManager);

async::Task<void> v1_api_support_send_forgotten_pin(http::Server* srv, std::shared_ptr<http::Context> ctx,
                                                    std::string email, std::string pin, std::shared_ptr<db::Database> db,
                                                    std::shared_ptr<SettingsManager> settingsManager,
                                                    std::shared_ptr<CertManager> certManager);

async::Task<void> mii_image(http::Server* srv, std::shared_ptr<http::Context> ctx,
                            std::shared_ptr<db::Database> db,
                            std::shared_ptr<SettingsManager> settingsManager,
                            std::shared_ptr<CertManager> certManager);

std::unique_ptr<http::Response> createError(http::Version version, int code, const std::string& message, const std::string& cause, int httpStatus);

async::Task<void> errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx);

std::unique_ptr<http::Response> prepareResponse(http::Version version, int httpStatus = HTTP_STATUS_OK);
std::unique_ptr<http::Response> prepareResponse(http::Version version, pugi::xml_document& doc, int httpStatus = HTTP_STATUS_OK);

bool checkDeviceCert(const std::string& cert, EVP_PKEY* pubKey, std::string& deviceId);
async::Task<bool> checkOauthToken(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                                  const std::shared_ptr<db::Database>& db, crypto::AccountToken& token);
async::Task<std::optional<uint32_t>> checkHashedBasicAuth(std::shared_ptr<db::Database> db,
                                                          std::shared_ptr<http::Context> ctx);

bool checkRequestParams(const std::shared_ptr<http::Request>& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<CertManager>& certManager, std::unique_ptr<http::Response>& resOut,
                        bool checkDevice = true);

bool checkEmailAddress(const std::string& address);

bool sendEmail(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsManager,
               mailio::message& msg);

bool init(const std::shared_ptr<Logger::Logger>& logger);

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<CertManager> certMgr, std::shared_ptr<db::Database> db);

} // namespace acc

#endif //SPLATOON_SERVER_ACCOUNT_HPP
