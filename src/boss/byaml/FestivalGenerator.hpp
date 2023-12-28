#ifndef SPLATOON_SERVER_FESTIVALGENERATOR_HPP
#define SPLATOON_SERVER_FESTIVALGENERATOR_HPP

#include <string>
#include <map>
#include <vector>
#include <array>
#include <chrono>
#include "byaml.hpp"

namespace boss {

    namespace festival {

        using time_point = std::chrono::system_clock::time_point;

        enum class Language {
            EUROPEAN_GERMAN,
            EUROPEAN_ENGLISH,
            EUROPEAN_SPANISH,
            EUROPEAN_FRENCH,
            EUROPEAN_ITALIAN,
            JAPANESE,
            AMERICAN_ENGLISH,
            AMERICAN_SPANISH,
            AMERICAN_FRENCH
        };

        constexpr std::array<Language, 9> LANGUAGES = {
                Language::EUROPEAN_GERMAN,
                Language::EUROPEAN_ENGLISH,
                Language::EUROPEAN_SPANISH,
                Language::EUROPEAN_FRENCH,
                Language::EUROPEAN_ITALIAN,
                Language::JAPANESE,
                Language::AMERICAN_ENGLISH,
                Language::AMERICAN_SPANISH,
                Language::AMERICAN_FRENCH
        };

        enum class Speaker {
            IDOL_LEFT, // Callie
            IDOL_RIGHT // Marie
        };

        enum class Command {
            SPEAK_RAW_TEXT
        };

        enum class Emotion {
            NORMAL_TALK,
            GREETING,
            HAPPY,
            ANGRY,
            SURPRISED,
            BORED
        };

        enum class Gamemode {
            TURF_WAR,
            SPLAT_ZONES,
            TOWER_CONTROL,
            RAINMAKER
        };

        enum class Stage {
            URCHIN_UNDERPASS = 0,
            WALLEYE_WAREHOUSE = 1,
            SALTSPRAY_RIG = 2,
            AROWANA_MALL = 3,
            BLACKBELLY_SKATEPARK = 4,
            PORT_MACKEREL = 6,
            KELP_DOME = 7,
            BLUEFIN_DEPOT = 9,
            MORAY_TOWERS = 8,
            CAMP_TRIGGERFISH = 5,
            FLOUNDER_HEIGHTS = 11,
            HAMMERHEAD_BRIDGE = 10,
            MUSEUM_D_ALFONSINO = 12,
            MAHIMAHI_RESORT = 15,
            PIRANHA_PIT = 14,
            ANCHO_V_GAMES = 13
        };

        struct DialogueLine {
            Command command;
            Emotion emotion;
            Speaker speaker;
            std::string text;
            bool waitButton;
        };

        struct Dialogue {
            std::map<Language, std::vector<DialogueLine>> lines;
        };

        struct Color {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };

        struct TeamInfo {
            Color color;
            std::map<Language, std::string> names;
            std::map<Language, std::string> shortNames;
        };

        struct FestivalInfo {
            int id = 0;
            int battleResultRate = 1;
            bool lowPopulationNotJP = true;
            bool separateMatchingJP = true;
            Dialogue announceNews;
            Dialogue startNews;
            Dialogue resultANews;
            Dialogue resultBNews;
            Gamemode gamemode = Gamemode::TURF_WAR;
            std::array<Stage, 3> stages{};
            TeamInfo teamA;
            TeamInfo teamB;
            Color neutralColor{};
            time_point announceTime;
            time_point startTime;
            time_point endTime;
            time_point resultTime;
            time_point afterFesBonusStart;

            Language backupLanguage = Language::EUROPEAN_ENGLISH; // Used if a language is not available
        };

    } // namespace festival

    std::string getLanguageString(festival::Language language);
    std::string getSpeakerString(festival::Speaker speaker);
    std::string getCommandString(festival::Command command);
    std::string getEmotionString(festival::Emotion emotion);
    std::string getGamemodeString(festival::Gamemode gamemode);
    std::string getColorString(const festival::Color& color);

    std::shared_ptr<byaml::DictionaryNode> generateNewsDict(const festival::Dialogue& dialogue,
                                                            festival::Language backupLanguage, const std::string& newsType);
    std::shared_ptr<byaml::DictionaryNode> generateTeamDict(const festival::TeamInfo& teamInfo, festival::Language backupLanguage);
    byaml::Byaml generateFestivalByaml(const festival::FestivalInfo& festivalInfo);

} // namespace boss

#endif //SPLATOON_SERVER_FESTIVALGENERATOR_HPP
