#ifndef SPLATOON_SERVER_MANAGEMENT_HPP
#define SPLATOON_SERVER_MANAGEMENT_HPP

#include <map>

#include "../../db/database.hpp"
#include "../../grpc/channelPool.hpp"
#include "../server.hpp"
#include "../../settingsManager.hpp"

namespace mgm {

// TODO - Add authentication & authorization
// TODO - Allow for SSL
// TODO - Security Status for accounts server

using json = nlohmann::json;

enum class ManagementError {
    SUCCESS = 0,
    BAD_REQUEST = 4000,
    PERMISSION_DENIED = 4010,
    NOT_FOUND = 4040,
    METHOD_NOT_ALLOWED = 4050,
    CONFLICT = 4090,
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

// Device management endpoints
async::Task<void> mgm_list_devices(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_create_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_get_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t deviceId);
async::Task<void> mgm_update_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t deviceId);
async::Task<void> mgm_delete_device(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t deviceId);

// Account management endpoints
async::Task<void> mgm_list_accounts(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_create_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_get_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);
async::Task<void> mgm_update_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);
async::Task<void> mgm_delete_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);
async::Task<void> mgm_get_account_by_username(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);

// Account email & mii
async::Task<void> mgm_update_account_email(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);
async::Task<void> mgm_set_account_mii(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);

// Account CEMU files
async::Task<void> mgm_get_cemu_files(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);

// Account agreements
async::Task<void> mgm_add_account_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);
async::Task<void> mgm_remove_account_agreement(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);

// Account-device ownership
async::Task<void> mgm_link_device_to_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid);
async::Task<void> mgm_unlink_device_from_account(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId);
async::Task<void> mgm_update_account_device_status(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId);

// Account-device attributes
async::Task<void> mgm_list_account_device_attributes(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId);
async::Task<void> mgm_set_account_device_attribute(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId, const std::string& attributeName);
async::Task<void> mgm_remove_account_device_attribute(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, uint32_t pid, uint32_t deviceId, const std::string& attributeName);

// Friends management endpoints
async::Task<void> mgm_get_friends_client_count(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);

// Splatoon management endpoints
async::Task<void> mgm_get_splatoon_client_count(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_get_splatoon_lobby_count(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);
async::Task<void> mgm_get_splatoon_lobbies(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);

// Boss management endpoints (festivals & map rotation)
async::Task<void> mgm_get_festivals(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_get_active_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_save_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_delete_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb, int festivalId);
async::Task<void> mgm_switch_active_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_get_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_update_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_randomize_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);

std::unique_ptr<http::Response> createError(const std::shared_ptr<http::Context>& ctx, ManagementError code, const std::string& message, const std::string& corsOrigin, bool& keepAlive, int httpStatus);

async::Task<void> errorHandler(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr);

std::unique_ptr<http::Response> prepareResponse(const std::shared_ptr<http::Context>& ctx, bool& keepAlive, std::string corsOrigin, int httpStatus = HTTP_STATUS_OK);
std::unique_ptr<http::Response> prepareResponse(const std::shared_ptr<http::Context>& ctx, const json& body, bool& keepAlive, const std::string &corsOrigin, int httpStatus = HTTP_STATUS_OK);
std::unique_ptr<http::Response> prepareCORSPreflightResponse(const std::shared_ptr<http::Context>& ctx, const std::shared_ptr<SettingsManager>& settingsMgr, const std::string& allowedMethods, bool& keepAlive);

void registerRoutes(const std::shared_ptr<http::Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr,
                    const std::shared_ptr<db::Database>& mgmDb, const std::shared_ptr<Logger::Logger>& logger);

} // namespace mgm

#endif //SPLATOON_SERVER_MANAGEMENT_HPP

