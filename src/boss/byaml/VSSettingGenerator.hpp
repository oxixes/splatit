#ifndef SPLATOON_SERVER_VSSETTINGGENERATOR_HPP
#define SPLATOON_SERVER_VSSETTINGGENERATOR_HPP

#include <chrono>
#include "byaml.hpp"
#include "FestivalGenerator.hpp"

// Maybe we could make some of these configurable?

#define ADD_FIRST_MATCHING_TIME 30
#define ADD_MATCHING_TIME 0
#define BOTTLENECK_THRESHOLD_TIME 480
#define DISCONNECT_BY_MEMORY_HASH true

#define MAPS_FIRST_APPEARANCE "2015-04-17"
#define RULE_FIRST_APPEARANCE "2015-04-17"
#define WEAPON_UNLOCK "2015-04-17"

#define TIMEOUT_AFTER_JOIN 120
#define VERSION 3
#define WAIT_MATCHING_TIME 25
#define WEB_POST true

#define PHASE_COUNT 180
#define PHASE_DURATION 4

namespace boss {

    struct MapFirstAppearance {
        uint32_t mapId;
        std::chrono::system_clock::time_point date;
    };

    struct RuleFirstAppearance {
        boss::festival::Gamemode gachiRule;
        std::chrono::system_clock::time_point date;
    };

    struct WeaponUnlockEntry {
        uint32_t weaponSetId;
        std::chrono::system_clock::time_point date;
    };

    struct PhaseConfig {
        boss::festival::Gamemode gachiRule;
        boss::festival::Gamemode regularRule;
        std::vector<uint32_t> gachiStages;
        std::vector<uint32_t> regularStages;
        uint32_t duration;
    };

    struct VSSettingFullConfig {
        uint32_t addFirstMatchingTime;
        uint32_t addMatchingTime;
        std::chrono::system_clock::time_point afterFesBonusStart;
        uint32_t bottleneckThresholdFrame;
        bool disconnectByMemoryHash;
        std::chrono::system_clock::time_point datetime;
        std::vector<MapFirstAppearance> mapFirstAppear;
        std::vector<PhaseConfig> phases;
        std::vector<RuleFirstAppearance> ruleFirstAppear;
        uint32_t timeoutAfterJoin;
        uint32_t version;
        uint32_t waitMatchingTime;
        std::vector<WeaponUnlockEntry> weaponUnlock;
        bool webPost;
    };

    inline const std::vector<uint32_t> MAPS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    inline const std::vector<std::string> RULES = {"cVar", "cVlf", "cVgl"};

    inline const std::vector<uint32_t> WEAPON_SETS = {
        1000, 1001, 1002, 1020, 1021, 1032, 1042, 1060, 1061, 1062, 1072, 1081, 1091, 1110, 1111, 1130,
        1131, 1132, 1150, 1151, 1160, 1161, 1170, 1171, 1172, 2000, 2001, 2012, 2022, 2030, 2031, 2032,
        2040, 2041, 3000, 3001, 3002, 3010, 3011, 3020, 3021, 4002, 4012, 4022, 4031, 4040, 4041, 4050,
        4051, 4052, 5000, 5001, 5002, 5010, 5011, 5012, 5020, 5021
    };

    std::string gamemodeToGameString(boss::festival::Gamemode mode);

    byaml::Byaml generateVSSettingByaml(std::chrono::system_clock::time_point afterFesBonusStartTime);
    byaml::Byaml generateVSSettingByamlFromConfig(const VSSettingFullConfig& config);
}

#endif //SPLATOON_SERVER_VSSETTINGGENERATOR_HPP
