#ifndef SPLATOON_SERVER_BOSS_UTILS_HPP
#define SPLATOON_SERVER_BOSS_UTILS_HPP

#include <nlohmann/json.hpp>

#include "byaml/FestivalGenerator.hpp"
#include "byaml/VSSettingGenerator.hpp"
#include "../logger.hpp"
#include "../settingsManager.hpp"

using json = nlohmann::json;

namespace boss {

extern json bossManifest; // Defined in /boss/utils.cpp

bool init(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsMgr);

void createFestival(const festival::FestivalInfo& festivalInfo, const std::vector<uint8_t>& bodyTeamA,
                    const std::vector<uint8_t>& bodyTeamB, const std::vector<uint8_t>& panelTexture,
                    const fs::path& bossDir);
void createVSSetting(std::chrono::system_clock::time_point afterFesBonusStartTime, const fs::path& bossDir);

} // namespace boss

#endif //SPLATOON_SERVER_BOSS_UTILS_HPP
