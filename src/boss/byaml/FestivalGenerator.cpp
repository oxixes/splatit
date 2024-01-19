#include "FestivalGenerator.hpp"
#include "../../util/util.hpp"

namespace boss {

    std::string getLanguageString(festival::Language language) {
        switch (language) {
            case festival::Language::EUROPEAN_GERMAN:
                return "EUde";
            case festival::Language::EUROPEAN_ENGLISH:
                return "EUen";
            case festival::Language::EUROPEAN_SPANISH:
                return "EUes";
            case festival::Language::EUROPEAN_FRENCH:
                return "EUfr";
            case festival::Language::EUROPEAN_ITALIAN:
                return "EUit";
            case festival::Language::JAPANESE:
                return "JPja";
            case festival::Language::AMERICAN_ENGLISH:
                return "USen";
            case festival::Language::AMERICAN_SPANISH:
                return "USes";
            case festival::Language::AMERICAN_FRENCH:
                return "USfr";
        }

        return "";
    }

    std::string getSpeakerString(festival::Speaker speaker) {
        switch (speaker) {
            case festival::Speaker::IDOL_LEFT:
                return "cIdolLeft";
            case festival::Speaker::IDOL_RIGHT:
                return "cIdolRight";
            case festival::Speaker::IDOL_ALL:
                return "cIdolAll";
        }

        return "";
    }

    std::string getCommandString(festival::Command command) {
        switch (command) {
            case festival::Command::SPEAK_RAW_TEXT:
                return "SpeakRawText";
        }

        return "";
    }

    std::string getEmotionString(festival::Emotion emotion) {
        switch (emotion) {
            case festival::Emotion::NORMAL_TALK:
                return "cNormalTalk";
            case festival::Emotion::GREETING:
                return "cGreeting";
            case festival::Emotion::HAPPY:
                return "cHappy";
            case festival::Emotion::ANGRY:
                return "cAngry";
            case festival::Emotion::SURPRISED:
                return "cSurprised";
            case festival::Emotion::BORED:
                return "cBored";
            case festival::Emotion::FEED:
                return "cFeed";
        }

        return "";
    }

    std::string getGamemodeString(festival::Gamemode gamemode) {
        switch (gamemode) {
            case festival::Gamemode::TURF_WAR:
                return "cPnt";
            case festival::Gamemode::SPLAT_ZONES:
                return "cVar";
            case festival::Gamemode::TOWER_CONTROL:
                return "cVlf";
            case festival::Gamemode::RAINMAKER:
                return "cVgl";
        }

        return "";
    }

    std::string getColorString(const festival::Color& color) {
        // The color follows the RGBA format, having values from 0 to 1, with 2 or 1 decimal places, separated by a comma.
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << (float) color.r / 255 << "," << (float) color.g / 255 << "," << (float) color.b / 255 << ","
            << std::setprecision(1) << (float) color.a / 255;
        return ss.str();
    }

    std::shared_ptr<byaml::DictionaryNode> generateNewsDict(const festival::Dialogue& dialogue,
                                                            festival::Language backupLanguage, const std::string& newsType) {
        if (!dialogue.lines.contains(backupLanguage)) {
            throw std::runtime_error("The backup language is not present in the dialogue.");
        }

        std::map<std::string, std::shared_ptr<byaml::Node>> newsDictMap = {
                {"NewsType", std::make_shared<byaml::StringNode>(newsType)}
        };

        // We iterate over ALL languages, even if they are not present in the dialogue, to make sure the news are
        // displayed in all languages (we use the backup language if the news are not available in a language).
        for (auto language : festival::LANGUAGES) {
            const std::vector<festival::DialogueLine>* lines;
            if (dialogue.lines.contains(language)) {
                lines = &dialogue.lines.at(language);
            } else {
                lines = &dialogue.lines.at(backupLanguage);
            }

            std::vector<std::shared_ptr<byaml::Node>> newsLines;
            for (const auto& line : *lines) {
                std::map<std::string, std::shared_ptr<byaml::Node>> lineMap = {
                        {"Command", std::make_shared<byaml::StringNode>(getCommandString(line.command))},
                        {"Emotion", std::make_shared<byaml::StringNode>(getEmotionString(line.emotion))},
                        {"Speaker", std::make_shared<byaml::StringNode>(getSpeakerString(line.speaker))},
                        {"Text", std::make_shared<byaml::StringNode>(line.text)},
                        {"WaitButton", std::make_shared<byaml::BoolNode>(line.waitButton)}
                };

                newsLines.push_back(std::make_shared<byaml::DictionaryNode>(lineMap));
            }

            newsDictMap.insert({getLanguageString(language), std::make_shared<byaml::ArrayNode>(newsLines)});
        }

        return std::make_shared<byaml::DictionaryNode>(newsDictMap);
    }

    std::shared_ptr<byaml::DictionaryNode> generateTeamDict(const festival::TeamInfo& teamInfo, festival::Language backupLanguage) {
        if (!teamInfo.names.contains(backupLanguage)) {
            throw std::runtime_error("The backup language is not present in the team info.");
        }

        std::map<std::string, std::shared_ptr<byaml::Node>> namesMap;
        for (auto language : festival::LANGUAGES) {
            if (teamInfo.names.contains(language)) {
                namesMap.insert({getLanguageString(language), std::make_shared<byaml::StringNode>(teamInfo.names.at(language))});
            } else {
                namesMap.insert({getLanguageString(language), std::make_shared<byaml::StringNode>(teamInfo.names.at(backupLanguage))});
            }
        }

        std::map<std::string, std::shared_ptr<byaml::Node>> shortNamesMap;
        for (auto language : festival::LANGUAGES) {
            if (teamInfo.shortNames.contains(language)) {
                shortNamesMap.insert({getLanguageString(language), std::make_shared<byaml::StringNode>(teamInfo.shortNames.at(language))});
            } else {
                shortNamesMap.insert({getLanguageString(language), std::make_shared<byaml::StringNode>(teamInfo.shortNames.at(backupLanguage))});
            }
        }

        std::map<std::string, std::shared_ptr<byaml::Node>> teamDictMap = {
                {"Color", std::make_shared<byaml::StringNode>(getColorString(teamInfo.color))},
                {"Name", std::make_shared<byaml::DictionaryNode>(namesMap)},
                {"ShortName", std::make_shared<byaml::DictionaryNode>(shortNamesMap)}
        };

        return std::make_shared<byaml::DictionaryNode>(teamDictMap);
    }

    byaml::Byaml generateFestivalByaml(const festival::FestivalInfo& festivalInfo) {
        std::vector<std::shared_ptr<byaml::Node>> stages;
        for (const auto& stage : festivalInfo.stages) {
            std::map<std::string, std::shared_ptr<byaml::Node>> stageMap = {
                    {"MapID", std::make_shared<byaml::IntegerNode>(static_cast<int32_t>(stage))},
            };

            stages.push_back(std::make_shared<byaml::DictionaryNode>(stageMap));
        }

        std::shared_ptr<byaml::ArrayNode> stagesArray = std::make_shared<byaml::ArrayNode>(stages);

        std::vector<std::shared_ptr<byaml::Node>> teams;
        teams.push_back(generateTeamDict(festivalInfo.teamA, festivalInfo.backupLanguage));
        teams.push_back(generateTeamDict(festivalInfo.teamB, festivalInfo.backupLanguage));

        std::map<std::string, std::shared_ptr<byaml::Node>> neutralTeam = {
                {"Color", std::make_shared<byaml::StringNode>(getColorString(festivalInfo.neutralColor))}
        };
        teams.push_back(std::make_shared<byaml::DictionaryNode>(neutralTeam));

        std::shared_ptr<byaml::ArrayNode> teamsArray = std::make_shared<byaml::ArrayNode>(teams);

        std::map<std::string, std::shared_ptr<byaml::Node>> times = {
                {"Announce", std::make_shared<byaml::StringNode>(util::formatTime(festivalInfo.announceTime))},
                {"End", std::make_shared<byaml::StringNode>(util::formatTime(festivalInfo.endTime))},
                {"Result", std::make_shared<byaml::StringNode>(util::formatTime(festivalInfo.resultTime))},
                {"Start", std::make_shared<byaml::StringNode>(util::formatTime(festivalInfo.startTime))}
        };

        std::shared_ptr<byaml::DictionaryNode> timesDict = std::make_shared<byaml::DictionaryNode>(times);

        std::vector<std::shared_ptr<byaml::Node>> news;
        news.push_back(generateNewsDict(festivalInfo.announceNews, festivalInfo.backupLanguage, "Announce"));
        news.push_back(generateNewsDict(festivalInfo.startNews, festivalInfo.backupLanguage, "Start"));
        news.push_back(generateNewsDict(festivalInfo.resultANews, festivalInfo.backupLanguage, "ResultA"));
        news.push_back(generateNewsDict(festivalInfo.resultBNews, festivalInfo.backupLanguage, "ResultB"));

        std::shared_ptr<byaml::ArrayNode> newsArray = std::make_shared<byaml::ArrayNode>(news);

        std::map<std::string, std::shared_ptr<byaml::Node>> festivalMap = {
                {"BattleResultRate", std::make_shared<byaml::IntegerNode>(festivalInfo.battleResultRate)},
                {"FestivalId", std::make_shared<byaml::IntegerNode>(festivalInfo.id)},
                {"LowPopulationNotJP", std::make_shared<byaml::BoolNode>(festivalInfo.lowPopulationNotJP)},
                {"News", newsArray},
                {"Rule", std::make_shared<byaml::StringNode>(getGamemodeString(festivalInfo.gamemode))},
                {"SeparateMatchingJP", std::make_shared<byaml::BoolNode>(festivalInfo.separateMatchingJP)},
                {"Stages", stagesArray},
                {"Teams", teamsArray},
                {"Time", timesDict},
                {"Version", std::make_shared<byaml::IntegerNode>(0)}
        };

        return byaml::Byaml(std::make_shared<byaml::DictionaryNode>(festivalMap));
    }

}