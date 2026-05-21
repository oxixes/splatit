#ifndef SPLATOON_SERVER_BOSS_MANAGEMENT_HPP
#define SPLATOON_SERVER_BOSS_MANAGEMENT_HPP

#include <memory>

#include "../../db/database.hpp"
#include "../../settingsManager.hpp"
#include "../server.hpp"

namespace mgm {

async::Task<void> mgm_get_festivals(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_get_active_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_save_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_delete_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb, int festivalId);
async::Task<void> mgm_switch_active_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);

async::Task<void> mgm_get_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_update_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);
async::Task<void> mgm_randomize_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb);

async::Task<void> initManagementData(const std::shared_ptr<db::Database>& mgmtDb, const std::shared_ptr<Logger::Logger>& logger);

} // namespace mgm

#endif //SPLATOON_SERVER_BOSS_MANAGEMENT_HPP
