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

http::Response v1_api_admin_time(const http::Request& req, bool& shouldClose);
http::Response v1_api_admin_mapped_ids(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                       const http::Request& req, sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                                       const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                       const std::function<void(unsigned int)>& unregisterCloseCall,
                                       const std::shared_ptr<SettingsManager>& settingsManager,
                                       const std::shared_ptr<CertManager>& certManager);

http::Response v1_api_access_token_gen(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                       const http::Request& req, sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                                       const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                       const std::function<void(unsigned int)>& unregisterCloseCall,
                                       const std::shared_ptr<SettingsManager>& settingsManager,
                                       const std::shared_ptr<CertManager>& certManager);

http::Response v1_api_provider_nex_token(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db,
                                         const http::Request& req, sock::IPv4Addr client, bool& shouldStop, bool& shouldClose,
                                         const std::function<unsigned int(std::function<void()>)>& registerCloseCall,
                                         const std::function<void(unsigned int)>& unregisterCloseCall,
                                         const std::shared_ptr<SettingsManager>& settingsManager,
                                         const std::shared_ptr<CertManager>& certManager);

http::Response createError(http::Version version, int code, const std::string& message, const std::string& cause,
                           bool& shouldClose);

http::Response errorHandler(const std::shared_ptr<Logger::Logger>& logger, const http::Request& req, sock::IPv4Addr client,
                            int httpStatus);

http::Response prepareResponse(http::Version version);
http::Response prepareResponse(http::Version version, pugi::xml_document& doc);

bool checkDeviceCert(const std::string& cert, EVP_PKEY* pubKey);

bool checkRequestParams(const http::Request& req, const std::shared_ptr<SettingsManager>& settingsManager,
                        const std::shared_ptr<CertManager>& certManager, http::Response* resOut, bool& shouldClose);

void registerRoutes(const std::shared_ptr<HTTP_Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<CertManager> certMgr, std::shared_ptr<db::Database> db);

} // namespace acc

#endif //SPLATOON_SERVER_ACCOUNT_HPP
