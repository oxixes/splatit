#ifndef SPLATOON_SERVER_MANAGEMENT_HPP
#define SPLATOON_SERVER_MANAGEMENT_HPP

#include <map>

#include "../../db/database.hpp"
#include "../../grpc/channelPool.hpp"
#include "../server.hpp"
#include "../../settingsManager.hpp"

namespace mgm {

using json = nlohmann::json;

enum class ManagementError {
    SUCCESS = 0,
    BAD_REQUEST = 4000,
    NOT_FOUND = 4040,
    METHOD_NOT_ALLOWED = 4050,
    INTERNAL_ERROR = 5000,
    BAD_GATEWAY = 5020,
};

extern std::shared_ptr<grpcimpl::ChannelPool> channelPool;
extern std::map<ServerType, std::vector<sock::IPv4Addr>> serverHosts;
extern std::map<ServerType, size_t> serverHostsIndexRoundRobin;

async::Task<void> mgm_status(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_server_status(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);

// Agreement management endpoints
async::Task<void> mgm_get_agreements(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_publish_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_delete_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);

std::unique_ptr<http::Response> createError(const std::shared_ptr<http::Context>& ctx, ManagementError code, const std::string& message, const std::string& corsOrigin, bool& keepAlive, int httpStatus);

async::Task<void> errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);

std::unique_ptr<http::Response> prepareResponse(const std::shared_ptr<http::Context>& ctx, bool& keepAlive, std::string corsOrigin, int httpStatus = HTTP_STATUS_OK);
std::unique_ptr<http::Response> prepareResponse(const std::shared_ptr<http::Context>& ctx, const json& body, bool& keepAlive, const std::string &corsOrigin, int httpStatus = HTTP_STATUS_OK);
std::unique_ptr<http::Response> prepareCORSPreflightResponse(const std::shared_ptr<http::Context>& ctx, const std::shared_ptr<SettingsManager>& settingsMgr, const std::string& allowedMethods, bool& keepAlive);

void registerRoutes(const std::shared_ptr<http::Server>& server, std::shared_ptr<SettingsManager> settingsMgr,
                    std::shared_ptr<db::Database> db);

} // namespace mgm

#endif //SPLATOON_SERVER_MANAGEMENT_HPP