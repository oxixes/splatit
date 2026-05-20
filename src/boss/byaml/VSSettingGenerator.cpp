#include "VSSettingGenerator.hpp"
#include "../../util/util.hpp"

#include <chrono>
#include <random>

namespace boss {

byaml::Byaml generateVSSettingByaml(std::chrono::system_clock::time_point afterFesBonusStartTime) {
    std::shared_ptr<byaml::IntegerNode> addFirstMatchingTime = std::make_shared<byaml::IntegerNode>(ADD_FIRST_MATCHING_TIME);
    std::shared_ptr<byaml::IntegerNode> addMatchingTime = std::make_shared<byaml::IntegerNode>(ADD_MATCHING_TIME);
    std::shared_ptr<byaml::StringNode> afterFesBonusStart = std::make_shared<byaml::StringNode>(util::formatTime(afterFesBonusStartTime));
    std::shared_ptr<byaml::IntegerNode> bottleneckThresholdTime = std::make_shared<byaml::IntegerNode>(BOTTLENECK_THRESHOLD_TIME);

    // Get current time
    std::string nowStr = util::formatTime(std::chrono::system_clock::now());

    std::shared_ptr<byaml::StringNode> datetime = std::make_shared<byaml::StringNode>(nowStr);
    std::shared_ptr<byaml::BoolNode> disconnectByMemoryHash = std::make_shared<byaml::BoolNode>(DISCONNECT_BY_MEMORY_HASH);

    std::vector<uint32_t> maps = MAPS;
    std::vector<std::string> rules = RULES;
    std::vector<uint32_t> weaponSets = WEAPON_SETS;

    std::vector<std::shared_ptr<byaml::Node>> mapsFirstAppearance;
    for (uint32_t map : maps) {
        std::shared_ptr<byaml::IntegerNode> mapId = std::make_shared<byaml::IntegerNode>(map);
        std::shared_ptr<byaml::StringNode> date = std::make_shared<byaml::StringNode>(MAPS_FIRST_APPEARANCE);
        std::map<std::string, std::shared_ptr<byaml::Node>> mapFirstAppearance = {
                {"Date", date},
                {"MapID", mapId}
        };
        std::shared_ptr<byaml::DictionaryNode> mapFirstAppearanceDict = std::make_shared<byaml::DictionaryNode>(std::move(mapFirstAppearance));

        mapsFirstAppearance.push_back(mapFirstAppearanceDict);
    }

    std::shared_ptr<byaml::ArrayNode> mapsFirstAppearanceArray = std::make_shared<byaml::ArrayNode>(std::move(mapsFirstAppearance));

    // This code generates the phases, which dictates the maps and rules in rotation. It's the only non-static part of the VS setting.
    // We create a random nnumber generator to select the maps and rules.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> mapDist(0, (int32_t) maps.size() - 1);
    std::uniform_int_distribution<> ruleDist(0, (int32_t) rules.size() - 1);

    std::vector<std::shared_ptr<byaml::Node>> phases;
    for (int i = 0; i < PHASE_COUNT; i++) {
        std::string gachiRuleStr = rules[ruleDist(gen)];
        std::string regularRuleStr = "cPnt"; // It's always turf war in regular battles

        uint32_t gachiStage1 = maps[mapDist(gen)];
        uint32_t gachiStage2 = maps[mapDist(gen)];
        while (gachiStage2 == gachiStage1) { // Make sure the two stages are different
            gachiStage2 = maps[mapDist(gen)];
        }

        uint32_t regularStage1 = maps[mapDist(gen)];
        uint32_t regularStage2 = maps[mapDist(gen)];
        while (regularStage2 == regularStage1) { // Make sure the two stages are different
            regularStage2 = maps[mapDist(gen)];
        }

        std::shared_ptr<byaml::StringNode> gachiRule = std::make_shared<byaml::StringNode>(gachiRuleStr);
        std::shared_ptr<byaml::StringNode> regularRule = std::make_shared<byaml::StringNode>(regularRuleStr);

        std::shared_ptr<byaml::IntegerNode> gachiStage1Int = std::make_shared<byaml::IntegerNode>(gachiStage1);
        std::shared_ptr<byaml::DictionaryNode> gachiStage1Dict = std::make_shared<byaml::DictionaryNode>(std::map<std::string, std::shared_ptr<byaml::Node>>{
                {"MapID", gachiStage1Int}
        });
        std::shared_ptr<byaml::IntegerNode> gachiStage2Int = std::make_shared<byaml::IntegerNode>(gachiStage2);
        std::shared_ptr<byaml::DictionaryNode> gachiStage2Dict = std::make_shared<byaml::DictionaryNode>(std::map<std::string, std::shared_ptr<byaml::Node>>{
                {"MapID", gachiStage2Int}
        });

        std::shared_ptr<byaml::IntegerNode> regularStage1Int = std::make_shared<byaml::IntegerNode>(regularStage1);
        std::shared_ptr<byaml::DictionaryNode> regularStage1Dict = std::make_shared<byaml::DictionaryNode>(std::map<std::string, std::shared_ptr<byaml::Node>>{
                {"MapID", regularStage1Int}
        });
        std::shared_ptr<byaml::IntegerNode> regularStage2Int = std::make_shared<byaml::IntegerNode>(regularStage2);
        std::shared_ptr<byaml::DictionaryNode> regularStage2Dict = std::make_shared<byaml::DictionaryNode>(std::map<std::string, std::shared_ptr<byaml::Node>>{
                {"MapID", regularStage2Int}
        });

        std::shared_ptr<byaml::ArrayNode> gachiStages = std::make_shared<byaml::ArrayNode>(std::vector<std::shared_ptr<byaml::Node>>{
                gachiStage1Dict,
                gachiStage2Dict
        });

        std::shared_ptr<byaml::ArrayNode> regularStages = std::make_shared<byaml::ArrayNode>(std::vector<std::shared_ptr<byaml::Node>>{
                regularStage1Dict,
                regularStage2Dict
        });

        int32_t durationInt = (i == PHASE_COUNT - 1) ? 87600 : PHASE_DURATION; // The last phase has a duration of 10 years
        std::shared_ptr<byaml::IntegerNode> duration = std::make_shared<byaml::IntegerNode>(durationInt);

        std::map<std::string, std::shared_ptr<byaml::Node>> phaseMap = {
                {"GachiRule", gachiRule},
                {"GachiStages", gachiStages},
                {"RegularRule", regularRule},
                {"RegularStages", regularStages},
                {"Time", duration}
        };

        std::shared_ptr<byaml::DictionaryNode> phase = std::make_shared<byaml::DictionaryNode>(std::move(phaseMap));

        phases.push_back(phase);
    }

    std::shared_ptr<byaml::ArrayNode> phasesArray = std::make_shared<byaml::ArrayNode>(std::move(phases));

    std::vector<std::shared_ptr<byaml::Node>> rulesFirstAppearance;
    for (const std::string& rule : rules) {
        std::shared_ptr<byaml::StringNode> date = std::make_shared<byaml::StringNode>(RULE_FIRST_APPEARANCE);
        std::shared_ptr<byaml::StringNode> ruleStr = std::make_shared<byaml::StringNode>(rule);
        std::map<std::string, std::shared_ptr<byaml::Node>> ruleFirstAppearance = {
                {"Date", date},
                {"GachiRule", ruleStr}
        };
        std::shared_ptr<byaml::DictionaryNode> ruleFirstAppearanceDict = std::make_shared<byaml::DictionaryNode>(std::move(ruleFirstAppearance));

        rulesFirstAppearance.push_back(ruleFirstAppearanceDict);
    }

    std::shared_ptr<byaml::ArrayNode> rulesFirstAppearanceArray = std::make_shared<byaml::ArrayNode>(std::move(rulesFirstAppearance));

    std::shared_ptr<byaml::IntegerNode> timeoutAfterJoin = std::make_shared<byaml::IntegerNode>(TIMEOUT_AFTER_JOIN);
    std::shared_ptr<byaml::IntegerNode> version = std::make_shared<byaml::IntegerNode>(VERSION);
    std::shared_ptr<byaml::IntegerNode> waitMatchingTime = std::make_shared<byaml::IntegerNode>(WAIT_MATCHING_TIME);

    std::vector<std::shared_ptr<byaml::Node>> weaponUnlock;
    for (uint32_t setId : weaponSets) {
        std::shared_ptr<byaml::IntegerNode> set = std::make_shared<byaml::IntegerNode>(setId);
        std::shared_ptr<byaml::StringNode> date = std::make_shared<byaml::StringNode>(WEAPON_UNLOCK);
        std::map<std::string, std::shared_ptr<byaml::Node>> weaponUnlockMap = {
                {"Date", date},
                {"WeaponSetID", set}
        };
        std::shared_ptr<byaml::DictionaryNode> weaponUnlockDict = std::make_shared<byaml::DictionaryNode>(std::move(weaponUnlockMap));

        weaponUnlock.push_back(weaponUnlockDict);
    }

    std::shared_ptr<byaml::ArrayNode> weaponUnlockArray = std::make_shared<byaml::ArrayNode>(std::move(weaponUnlock));

    std::shared_ptr<byaml::BoolNode> webPost = std::make_shared<byaml::BoolNode>(WEB_POST);

    std::map<std::string, std::shared_ptr<byaml::Node>> rootMap = {
            {"AddFirstMatchingTime", addFirstMatchingTime},
            {"AddMatchingTime", addMatchingTime},
            {"AfterFesBonusStart", afterFesBonusStart},
            {"BottleneckThreasholdFrame", bottleneckThresholdTime}, // Yes, it's spelled wrong in the game
            {"DateTime", datetime},
            {"DisconnectByMemoryHash", disconnectByMemoryHash},
            {"MapFirstAppear", mapsFirstAppearanceArray},
            {"Phases", phasesArray},
            {"RuleFirstAppear", rulesFirstAppearanceArray},
            {"TimeoutAfterJoin", timeoutAfterJoin},
            {"Version", version},
            {"WaitMatchingTime", waitMatchingTime},
            {"WeaponUnlock", weaponUnlockArray},
            {"WebPost", webPost}
    };

    std::shared_ptr<byaml::DictionaryNode> root = std::make_shared<byaml::DictionaryNode>(std::move(rootMap));

    return byaml::Byaml(root);
}

byaml::Byaml generateVSSettingByamlFromConfig(const VSSettingFullConfig& config) {
    std::shared_ptr<byaml::IntegerNode> addFirstMatchingTime = std::make_shared<byaml::IntegerNode>(config.addFirstMatchingTime);
    std::shared_ptr<byaml::IntegerNode> addMatchingTime = std::make_shared<byaml::IntegerNode>(config.addMatchingTime);
    std::shared_ptr<byaml::StringNode> afterFesBonusStart = std::make_shared<byaml::StringNode>(util::formatTime(config.afterFesBonusStart));
    std::shared_ptr<byaml::IntegerNode> bottleneckThresholdTime = std::make_shared<byaml::IntegerNode>(config.bottleneckThresholdFrame);

    std::shared_ptr<byaml::StringNode> datetime = std::make_shared<byaml::StringNode>(config.datetime);
    std::shared_ptr<byaml::BoolNode> disconnectByMemoryHash = std::make_shared<byaml::BoolNode>(config.disconnectByMemoryHash);

    std::vector<std::shared_ptr<byaml::Node>> mapsFirstAppearance;
    for (const auto& entry : config.mapFirstAppear) {
        std::shared_ptr<byaml::IntegerNode> mapId = std::make_shared<byaml::IntegerNode>(entry.mapId);
        std::shared_ptr<byaml::StringNode> date = std::make_shared<byaml::StringNode>(entry.date);
        std::map<std::string, std::shared_ptr<byaml::Node>> mapFirstAppearance = {
                {"Date", date},
                {"MapID", mapId}
        };
        std::shared_ptr<byaml::DictionaryNode> mapFirstAppearanceDict = std::make_shared<byaml::DictionaryNode>(std::move(mapFirstAppearance));
        mapsFirstAppearance.push_back(mapFirstAppearanceDict);
    }
    std::shared_ptr<byaml::ArrayNode> mapsFirstAppearanceArray = std::make_shared<byaml::ArrayNode>(std::move(mapsFirstAppearance));

    std::vector<std::shared_ptr<byaml::Node>> phases;
    for (const auto& phaseConfig : config.phases) {
        std::shared_ptr<byaml::StringNode> gachiRule = std::make_shared<byaml::StringNode>(phaseConfig.gachiRule);
        std::shared_ptr<byaml::StringNode> regularRule = std::make_shared<byaml::StringNode>(phaseConfig.regularRule);

        std::vector<std::shared_ptr<byaml::Node>> gachiStageNodes;
        for (uint32_t stage : phaseConfig.gachiStages) {
            std::shared_ptr<byaml::IntegerNode> stageInt = std::make_shared<byaml::IntegerNode>(stage);
            std::map<std::string, std::shared_ptr<byaml::Node>> stageMap = {
                    {"MapID", stageInt}
            };
            gachiStageNodes.push_back(std::make_shared<byaml::DictionaryNode>(std::move(stageMap)));
        }
        std::shared_ptr<byaml::ArrayNode> gachiStages = std::make_shared<byaml::ArrayNode>(std::move(gachiStageNodes));

        std::vector<std::shared_ptr<byaml::Node>> regularStageNodes;
        for (uint32_t stage : phaseConfig.regularStages) {
            std::shared_ptr<byaml::IntegerNode> stageInt = std::make_shared<byaml::IntegerNode>(stage);
            std::map<std::string, std::shared_ptr<byaml::Node>> stageMap = {
                    {"MapID", stageInt}
            };
            regularStageNodes.push_back(std::make_shared<byaml::DictionaryNode>(std::move(stageMap)));
        }
        std::shared_ptr<byaml::ArrayNode> regularStages = std::make_shared<byaml::ArrayNode>(std::move(regularStageNodes));

        std::shared_ptr<byaml::IntegerNode> duration = std::make_shared<byaml::IntegerNode>(phaseConfig.duration);

        std::map<std::string, std::shared_ptr<byaml::Node>> phaseMap = {
                {"GachiRule", gachiRule},
                {"GachiStages", gachiStages},
                {"RegularRule", regularRule},
                {"RegularStages", regularStages},
                {"Time", duration}
        };
        std::shared_ptr<byaml::DictionaryNode> phase = std::make_shared<byaml::DictionaryNode>(std::move(phaseMap));
        phases.push_back(phase);
    }
    std::shared_ptr<byaml::ArrayNode> phasesArray = std::make_shared<byaml::ArrayNode>(std::move(phases));

    std::vector<std::shared_ptr<byaml::Node>> rulesFirstAppearance;
    for (const auto& entry : config.ruleFirstAppear) {
        std::shared_ptr<byaml::StringNode> date = std::make_shared<byaml::StringNode>(entry.date);
        std::shared_ptr<byaml::StringNode> ruleStr = std::make_shared<byaml::StringNode>(entry.gachiRule);
        std::map<std::string, std::shared_ptr<byaml::Node>> ruleFirstAppearance = {
                {"Date", date},
                {"GachiRule", ruleStr}
        };
        std::shared_ptr<byaml::DictionaryNode> ruleFirstAppearanceDict = std::make_shared<byaml::DictionaryNode>(std::move(ruleFirstAppearance));
        rulesFirstAppearance.push_back(ruleFirstAppearanceDict);
    }
    std::shared_ptr<byaml::ArrayNode> rulesFirstAppearanceArray = std::make_shared<byaml::ArrayNode>(std::move(rulesFirstAppearance));

    std::shared_ptr<byaml::IntegerNode> timeoutAfterJoin = std::make_shared<byaml::IntegerNode>(config.timeoutAfterJoin);
    std::shared_ptr<byaml::IntegerNode> version = std::make_shared<byaml::IntegerNode>(config.version);
    std::shared_ptr<byaml::IntegerNode> waitMatchingTime = std::make_shared<byaml::IntegerNode>(config.waitMatchingTime);

    std::vector<std::shared_ptr<byaml::Node>> weaponUnlock;
    for (const auto& entry : config.weaponUnlock) {
        std::shared_ptr<byaml::IntegerNode> set = std::make_shared<byaml::IntegerNode>(entry.weaponSetId);
        std::shared_ptr<byaml::StringNode> date = std::make_shared<byaml::StringNode>(entry.date);
        std::map<std::string, std::shared_ptr<byaml::Node>> weaponUnlockMap = {
                {"Date", date},
                {"WeaponSetID", set}
        };
        std::shared_ptr<byaml::DictionaryNode> weaponUnlockDict = std::make_shared<byaml::DictionaryNode>(std::move(weaponUnlockMap));
        weaponUnlock.push_back(weaponUnlockDict);
    }
    std::shared_ptr<byaml::ArrayNode> weaponUnlockArray = std::make_shared<byaml::ArrayNode>(std::move(weaponUnlock));

    std::shared_ptr<byaml::BoolNode> webPost = std::make_shared<byaml::BoolNode>(config.webPost);

    std::map<std::string, std::shared_ptr<byaml::Node>> rootMap = {
            {"AddFirstMatchingTime", addFirstMatchingTime},
            {"AddMatchingTime", addMatchingTime},
            {"AfterFesBonusStart", afterFesBonusStart},
            {"BottleneckThreasholdFrame", bottleneckThresholdTime},
            {"DateTime", datetime},
            {"DisconnectByMemoryHash", disconnectByMemoryHash},
            {"MapFirstAppear", mapsFirstAppearanceArray},
            {"Phases", phasesArray},
            {"RuleFirstAppear", rulesFirstAppearanceArray},
            {"TimeoutAfterJoin", timeoutAfterJoin},
            {"Version", version},
            {"WaitMatchingTime", waitMatchingTime},
            {"WeaponUnlock", weaponUnlockArray},
            {"WebPost", webPost}
    };

    std::shared_ptr<byaml::DictionaryNode> root = std::make_shared<byaml::DictionaryNode>(std::move(rootMap));

    return byaml::Byaml(root);
}

}  // namespace boss