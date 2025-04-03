#include "utils.hpp"
#include "bfres/ftex.hpp"
#include "../crypto/tools.hpp"
#include "../util/util.hpp"
#include "../constants.hpp"

namespace boss {

json bossManifest;

bool readManifest(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsMgr) {
    fs::path bossManifestPath = settingsMgr->getBOSSPath() / "manifest.json";

    if (!fs::exists(bossManifestPath) || !fs::is_regular_file(bossManifestPath)) {
        logger->log(Logger::level::WARN, Logger::group::SETUP,
                    "The BOSS manifest file does not exist or is not a file.");
        return false;
    }

    try {
        std::ifstream bossManifestFile(bossManifestPath);
        bossManifest = json::parse(bossManifestFile);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while parsing the BOSS manifest file: " + std::string(e.what()));
        return false;
    }

    fs::path schemaFilePath = fs::path("boss.schema.json");
    if (!fs::exists(schemaFilePath) || !fs::is_regular_file(schemaFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The BOSS manifest schema file does not exist or is not a file, cannot validate BOSS manifest.");
        return false;
    }

    json schema;
    json_validator validator;
    try {
        std::ifstream schemaFile(schemaFilePath);
        schema = json::parse(schemaFile);
        validator.set_root_schema(schema);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while parsing the BOSS manifest schema file: " + std::string(e.what()));
        return false;
    }

    try {
        validator.validate(bossManifest);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The BOSS manifest file is invalid: " + std::string(e.what()));
        return false;
    }

    return true;
}

// Only called if the manifest file does not exist
bool createDefaultManifest(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsMgr) {
    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating default BOSS files.");

    fs::path bossManifestPath = settingsMgr->getBOSSPath() / "manifest.json";

    // Open default images
    std::vector<uint8_t> panelTexture;
    std::vector<uint8_t> bodyTeamA;
    std::vector<uint8_t> bodyTeamB;
    try {
        std::ifstream panelTextureFile("PanelTexture.default.png", std::ios::binary);
        std::ifstream bodyTeamAFile("BodyTeamA.default.png", std::ios::binary);
        std::ifstream bodyTeamBFile("BodyTeamB.default.png", std::ios::binary);

        if (!panelTextureFile.is_open() || !bodyTeamAFile.is_open() || !bodyTeamBFile.is_open()) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        "Failed to open default images for BOSS.");
            return false;
        }

        panelTexture = std::vector<uint8_t>((std::istreambuf_iterator<char>(panelTextureFile)),
                                          std::istreambuf_iterator<char>());
        bodyTeamA = std::vector<uint8_t>((std::istreambuf_iterator<char>(bodyTeamAFile)),
                                       std::istreambuf_iterator<char>());
        bodyTeamB = std::vector<uint8_t>((std::istreambuf_iterator<char>(bodyTeamBFile)),
                                       std::istreambuf_iterator<char>());
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while reading default images for BOSS: " + std::string(e.what()));
        return false;
    }

    time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    // Format the date as YYYY-MM-DDTHH:MM:SS+0000
    char nowBuff[25];
    std::strftime(nowBuff, 25, "%Y-%m-%dT%H:%M:%S+0000", std::gmtime(&now));
    std::string nowStr = nowBuff;

    bossManifest = {
            {"tasksheets", {
                    {EU_BOSS_APP_ID, {
                            {"titleId", EU_TITLE_ID},
                            {"tasksheets", {}}
                    }},
                    {US_BOSS_APP_ID, {
                            {"titleId", US_TITLE_ID},
                            {"tasksheets", {}}
                    }},
                    {JP_BOSS_APP_ID, {
                            {"titleId", JP_TITLE_ID},
                            {"tasksheets", {}}
                    }}
            }},
            {"policyLists", {
                    {"GB", {
                            {"major", 1},
                            {"minor", 0},
                            {"id", 1946},
                            {"defaultStop", false},
                            {"forceVersionUp", false},
                            {"updateTime", nowStr},
                            {"titles", {
                                    {"0005001010040000", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010040100", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010040200", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010047000", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010047100", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010047200", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010062000", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010062100", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010062200", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"0005001010066000", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"000500101004d000", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"000500101004d100", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                                    {"000500101004d200", {
                                            {"id", "G_ALTASK"},
                                            {"level", "MEDIUM"}
                                    }},
                            }}
                    }}
            }},
            {"backupPolicyCountry", "GB"}
    };

    festival::FestivalInfo defaultFestivalInfo{
        0x1CE0,
        1,
        true,
        false,
        {
                {
                        {festival::Language::AMERICAN_ENGLISH, {
                                {
                                        festival::Command::SPEAK_RAW_TEXT,
                                        festival::Emotion::NORMAL_TALK,
                                        festival::Speaker::IDOL_LEFT,
                                        "This is the announce text.",
                                        true
                                }
                        }}
                }
        },
        {
                {
                        {festival::Language::AMERICAN_ENGLISH, {
                                {
                                        festival::Command::SPEAK_RAW_TEXT,
                                        festival::Emotion::NORMAL_TALK,
                                        festival::Speaker::IDOL_LEFT,
                                        "This is the start text.",
                                        true
                                }
                        }}
                }
        },
        {
                {
                        {festival::Language::AMERICAN_ENGLISH, {
                                {
                                        festival::Command::SPEAK_RAW_TEXT,
                                        festival::Emotion::NORMAL_TALK,
                                        festival::Speaker::IDOL_LEFT,
                                        "This is the results for team A text.",
                                        true
                                }
                        }}
                }
        },
        {
                {
                        {festival::Language::AMERICAN_ENGLISH, {
                                {
                                        festival::Command::SPEAK_RAW_TEXT,
                                        festival::Emotion::NORMAL_TALK,
                                        festival::Speaker::IDOL_LEFT,
                                        "This is the results for team B text.",
                                        true
                                }
                        }}
                }
        },
        festival::Gamemode::SPLAT_ZONES,
        {
                festival::Stage::AROWANA_MALL,
                festival::Stage::FLOUNDER_HEIGHTS,
                festival::Stage::MAHIMAHI_RESORT
        },
        {
                {12, 24, 48, 255},
                {{festival::Language::AMERICAN_ENGLISH, "Team A"}}, // Long names
                {{festival::Language::AMERICAN_ENGLISH, "Team A"}}  // Short names
        },
        {
                {48, 24, 12, 255},
                {{festival::Language::AMERICAN_ENGLISH, "Team B"}}, // Long names
                {{festival::Language::AMERICAN_ENGLISH, "Team B"}}  // Short names
        },
        {100, 100, 100, 255},
        std::chrono::system_clock::now(),
        std::chrono::system_clock::now(),
        std::chrono::system_clock::now(),
        std::chrono::system_clock::now(),
        std::chrono::system_clock::now(),
        festival::Language::AMERICAN_ENGLISH
    };

    try {
        createFestival(defaultFestivalInfo, bodyTeamA, bodyTeamB, panelTexture, settingsMgr->getBOSSPath());
        createVSSetting(std::chrono::system_clock::now(), settingsMgr->getBOSSPath());
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, e.what());
        return false;
    }

    // Write the manifest to disk
    try {
        std::ofstream bossManifestFile(bossManifestPath);
        bossManifestFile << bossManifest.dump(4);
        bossManifestFile.close();
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while writing the BOSS manifest file to disk: " + std::string(e.what()));
        return false;
    }

    return true;
}

bool init(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsMgr) {
    fs::path bossManifestPath = settingsMgr->getBOSSPath() / "manifest.json";

    if (!readManifest(logger, settingsMgr) && fs::exists(bossManifestPath)) {
        return false;
    } else if (!fs::exists(bossManifestPath)) {
        if (!createDefaultManifest(logger, settingsMgr)) {
            return false;
        }
    }

    return true;
}

int getNextResourceId() {
    int id = 1000;
    for (const auto& [key, value] : bossManifest["tasksheets"].items()) {
        for (const auto& [tsKey, tsVal]: value["tasksheets"].items()) {
            for (const auto& [fileKey, fileVal]: tsVal["files"].items()) {
                id = std::max(id, fileVal["id"].get<int>());
            }
        }
    }

    return id + 1;
}

void createFestival(const festival::FestivalInfo& festivalInfo, const std::vector<uint8_t>& bodyTeamA,
                    const std::vector<uint8_t>& bodyTeamB, const std::vector<uint8_t>& panelTexture,
                    const fs::path& bossDir) {
    // Try to create the data directory if it doesn't exist
    fs::path dataDir = bossDir / "optdat2";
    if (!fs::exists(dataDir)) {
        if (!fs::create_directories(dataDir)) {
            // Failed to create the directory
            throw std::runtime_error("Failed to create the data directory for the festival.");
        }
    }

    byaml::Byaml festivalByaml = generateFestivalByaml(festivalInfo);

    std::string id = std::to_string(festivalInfo.id);

    auto encoder = std::make_shared<bfres::BC1_GTX_Encoder>(bfres::TileMode::M_2D_TILED_THIN1,
                                                                  bfres::AAMode::M_1X);
    bfres::FTEX panelFTEX(panelTexture, "Panel_" + id, encoder, 10, bfres::Usage::SURFACE_USE_TEXTURE,
                                960, 540);
    bfres::FTEX bodyTeamA_FTEX(bodyTeamA, "M_body_" + id + "_TeamA_Alb", encoder, 10, bfres::Usage::SURFACE_USE_TEXTURE,
                                512, 512);
    bfres::FTEX bodyTeamB_FTEX(bodyTeamB, "M_body_" + id + "_TeamB_Alb", encoder, 10, bfres::Usage::SURFACE_USE_TEXTURE,
                                512, 512);
    
    bfres::BFRES panelTextureBFRES("PanelTexture" + id);
    bfres::BFRES hapTextureBFRES("HapTexture" + id);

    panelTextureBFRES.addSubfile(std::make_unique<bfres::FTEX>(panelFTEX), "Panel_" + id);
    hapTextureBFRES.addSubfile(std::make_unique<bfres::FTEX>(bodyTeamA_FTEX), "M_body_" + id + "_TeamA_Alb");
    hapTextureBFRES.addSubfile(std::make_unique<bfres::FTEX>(bodyTeamB_FTEX), "M_body_" + id + "_TeamB_Alb");

    std::vector<uint8_t> festivalByamlData = festivalByaml.serialize();
    std::vector<uint8_t> panelTextureBFRESData = panelTextureBFRES.serialize();
    std::vector<uint8_t> hapTextureBFRESData = hapTextureBFRES.serialize();

    // Encrypt the data
    std::vector<uint8_t> encryptedFestivalByamlData = crypto::encryptBOSS(festivalByamlData);
    std::vector<uint8_t> encryptedPanelTextureBFRESData = crypto::encryptBOSS(panelTextureBFRESData);
    std::vector<uint8_t> encryptedHapTextureBFRESData = crypto::encryptBOSS(hapTextureBFRESData);

    // Calculate the hashes
    std::string md5FestivalByaml = util::bin2hex(crypto::MD5(encryptedFestivalByamlData));
    std::string md5PanelTextureBFRES = util::bin2hex(crypto::MD5(encryptedPanelTextureBFRESData));
    std::string md5HapTextureBFRES = util::bin2hex(crypto::MD5(encryptedHapTextureBFRESData));

    // Write the files to disk
    fs::path festivalByamlPath = dataDir / "Festival.byaml.boss";
    fs::path panelTextureBFRESPath = dataDir / "PanelTexture.bfres.boss";
    fs::path hapTextureBFRESPath = dataDir / "HapTexture.bfres.boss";

    try {
        std::ofstream festivalByamlFile(festivalByamlPath, std::ios::binary);
        std::ofstream panelTextureBFRESFile(panelTextureBFRESPath, std::ios::binary);
        std::ofstream hapTextureBFRESFile(hapTextureBFRESPath, std::ios::binary);

        festivalByamlFile.write(reinterpret_cast<const char*>(encryptedFestivalByamlData.data()), (long) encryptedFestivalByamlData.size());
        panelTextureBFRESFile.write(reinterpret_cast<const char*>(encryptedPanelTextureBFRESData.data()), (long) encryptedPanelTextureBFRESData.size());
        hapTextureBFRESFile.write(reinterpret_cast<const char*>(encryptedHapTextureBFRESData.data()), (long) encryptedHapTextureBFRESData.size());

        festivalByamlFile.close();
        panelTextureBFRESFile.close();
        hapTextureBFRESFile.close();
    } catch (const std::exception& e) {
        throw std::runtime_error("An error occurred while writing the festival files to disk: " + std::string(e.what()));
    }

    int resourceId = getNextResourceId();

    json::object_t optdat2 = {
            {"open", true},
            {"files", {
                    {md5FestivalByaml, {
                            {"id", resourceId++},
                            {"type", "AppData"},
                            {"filename", "Festival.byaml"},
                            {"path", "optdat2/Festival.byaml.boss"},
                            {"notify", {
                                    {"new", "app"},
                                    {"LED", false}
                            }}
                    }},
                    {md5PanelTextureBFRES, {
                            {"id", resourceId++},
                            {"type", "AppData"},
                            {"filename", "PanelTexture.bfres"},
                            {"path", "optdat2/PanelTexture.bfres.boss"},
                            {"notify", {
                                    {"new", "app"},
                                    {"LED", false}
                            }}
                    }},
                    {md5HapTextureBFRES, {
                            {"id", resourceId},
                            {"type", "AppData"},
                            {"filename", "HapTexture.bfres"},
                            {"path", "optdat2/HapTexture.bfres.boss"},
                            {"notify", {
                                    {"new", "app"},
                                    {"LED", false}
                            }}
                    }}
            }}
    };

    bossManifest["tasksheets"];

    bossManifest["tasksheets"][EU_BOSS_APP_ID]["tasksheets"]["optdat2"] = optdat2;
    bossManifest["tasksheets"][US_BOSS_APP_ID]["tasksheets"]["optdat2"] = optdat2;
    bossManifest["tasksheets"][JP_BOSS_APP_ID]["tasksheets"]["optdat2"] = optdat2;
}

void createVSSetting(std::chrono::system_clock::time_point afterFesBonusStartTime, const fs::path& bossDir) {
    // Try to create the data directory if it doesn't exist
    fs::path dataDir = bossDir / "schdat2";
    if (!fs::exists(dataDir)) {
        if (!fs::create_directories(dataDir)) {
            // Failed to create the directory
            throw std::runtime_error("Failed to create the data directory for the VSSetting.");
        }
    }

    byaml::Byaml vsSettingByaml = generateVSSettingByaml(afterFesBonusStartTime);

    std::vector<uint8_t> vsSettingByamlData = vsSettingByaml.serialize();

    // Encrypt the data
    std::vector<uint8_t> encryptedVSSettingByamlData = crypto::encryptBOSS(vsSettingByamlData);

    // Calculate the hash
    std::string md5VSSettingByaml = util::bin2hex(crypto::MD5(encryptedVSSettingByamlData));

    // Write the file to disk
    fs::path vsSettingByamlPath = dataDir / "VSSetting.byaml.boss";

    try {
        std::ofstream vsSettingByamlFile(vsSettingByamlPath, std::ios::binary);
        vsSettingByamlFile.write(reinterpret_cast<const char*>(encryptedVSSettingByamlData.data()), (long) encryptedVSSettingByamlData.size());
        vsSettingByamlFile.close();
    } catch (const std::exception& e) {
        throw std::runtime_error("An error occurred while writing the VSSetting file to disk: " + std::string(e.what()));
    }

    int resourceId = getNextResourceId();

    json::object_t schdat2 = {
            {"open", true},
            {"files", {
                    {md5VSSettingByaml, {
                            {"id", resourceId},
                            {"type", "AppData"},
                            {"filename", "VSSetting.byaml"},
                            {"path", "schdat2/VSSetting.byaml.boss"},
                            {"notify", {
                                    {"new", "app"},
                                    {"LED", false}
                            }}
                    }}
            }}
    };

    bossManifest["tasksheets"][EU_BOSS_APP_ID]["tasksheets"]["schdat2"] = schdat2;
    bossManifest["tasksheets"][US_BOSS_APP_ID]["tasksheets"]["schdat2"] = schdat2;
    bossManifest["tasksheets"][JP_BOSS_APP_ID]["tasksheets"]["schdat2"] = schdat2;
}



} // namespace boss