#ifndef SPLATOON_SERVER_BOSS_UTILS_HPP
#define SPLATOON_SERVER_BOSS_UTILS_HPP

#include <nlohmann/json.hpp>

#include "byaml/FestivalGenerator.hpp"
#include "byaml/VSSettingGenerator.hpp"
#include "../logger.hpp"
#include "../settingsManager.hpp"
#include "../db/database.hpp"

using json = nlohmann::json;

namespace boss {

async::Task<json> getManifest(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<db::Database>& db);

async::Task<void> createFestival(const festival::FestivalInfo& festivalInfo, const std::vector<uint8_t>& bodyTeamA,
                                 const std::vector<uint8_t>& bodyTeamB, const std::vector<uint8_t>& panelTexture,
                                 json& bossManifest, const std::shared_ptr<db::Database>& db);
async::Task<void> createVSSetting(std::chrono::system_clock::time_point afterFesBonusStartTime, json& bossManifest,
                                  const std::shared_ptr<db::Database>& db);

} // namespace boss

#endif //SPLATOON_SERVER_BOSS_UTILS_HPP
