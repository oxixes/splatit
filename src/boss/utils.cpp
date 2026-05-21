#include "utils.hpp"
#include "bfres/ftex.hpp"
#include "../crypto/tools.hpp"
#include "../util/util.hpp"
#include "../constants.hpp"

#include <algorithm>

namespace boss {

bool validateManifest(const std::shared_ptr<Logger::Logger>& logger, const json& bossManifest) {
    fs::path schemaFilePath = fs::path("boss.schema.json");
    if (!fs::exists(schemaFilePath) || !fs::is_regular_file(schemaFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::BOSS,
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
        logger->log(Logger::level::FAILURE, Logger::group::BOSS,
                    "An error occurred while parsing the BOSS manifest schema file: " + std::string(e.what()));
        return false;
    }

    try {
        validator.validate(bossManifest);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::BOSS,
                    "The BOSS manifest file is invalid: " + std::string(e.what()));
        return false;
    }

    return true;
}

// Only called if the manifest file does not exist
async::Task<bool> createDefaultManifest(const std::shared_ptr<Logger::Logger>& logger, json& bossManifest,
                                        std::shared_ptr<db::Database> db) {
    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating default BOSS files.");

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
            co_return false;
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
        co_return false;
    }

    time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    // Format the date as YYYY-MM-DDTHH:MM:SS+0000
    char nowBuff[25];
    std::strftime(nowBuff, 25, "%Y-%m-%dT%H:%M:%S+0000", std::gmtime(&now));
    std::string nowStr = nowBuff;

    bossManifest = {
            {"lastUpdate", now},
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
        0x1CE1,
        2,
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
        co_await createFestival(defaultFestivalInfo, bodyTeamA, bodyTeamB, panelTexture, bossManifest, db);
        co_await createVSSetting(std::chrono::system_clock::now(), bossManifest, db);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, e.what());
        co_return false;
    }

    co_return true;
}

async::Task<json> getManifest(const std::shared_ptr<Logger::Logger>& logger,
                              const std::shared_ptr<db::Database>& db) {
    auto getManifestCmd = db::Database::craftGetSettingCommand("manifest");
    db::Result results = co_await db->runCommand(std::move(getManifestCmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    if (results.hasData()) {
        try {
            json manifest = json::parse(results.getData<std::string>());
            if (!validateManifest(logger, manifest)) {
                throw std::runtime_error("Invalid BOSS manifest");
            }

            co_return manifest;
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, Logger::group::BOSS,
                        "Failed to parse BOSS manifest, will be overwritten: " + std::string(e.what()));
        }
    }

    std::shared_ptr<db::Database> session = db->createSession();
    if ((co_await session->startTransaction(true)).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    json manifest;
    if (!co_await createDefaultManifest(logger, manifest, session)) {
        co_await session->rollbackTransaction();
        throw std::runtime_error("Failed to create default BOSS manifest");
    }

    // Save the manifest to the database
    auto saveManifestCmd = db::Database::craftInsertOrUpdateSettingCommand("manifest", manifest.dump());
    results = co_await session->runCommand(std::move(saveManifestCmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        throw std::runtime_error("Database error");
    }

    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error");
    }

    co_return manifest;
}

int getNextResourceId(const json& bossManifest) {
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

async::Task<void> createFestival(const festival::FestivalInfo& festivalInfo, const std::vector<uint8_t>& bodyTeamA,
                    const std::vector<uint8_t>& bodyTeamB, const std::vector<uint8_t>& panelTexture,
                    json& bossManifest, const std::shared_ptr<db::Database>& db) {
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

    auto saveCmd = db::Database::craftInsertOrUpdateFileCommand(md5PanelTextureBFRES, encryptedPanelTextureBFRESData);
    db::Result results = co_await db->runCommand(std::move(saveCmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error while saving default panel texture for BOSS.");
    }

    saveCmd = db::Database::craftInsertOrUpdateFileCommand(md5HapTextureBFRES, encryptedHapTextureBFRESData);
    results = co_await db->runCommand(std::move(saveCmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error while saving default hap texture for BOSS.");
    }

    saveCmd = db::Database::craftInsertOrUpdateFileCommand(md5FestivalByaml, encryptedFestivalByamlData);
    results = co_await db->runCommand(std::move(saveCmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error while saving default festival byaml for BOSS.");
    }

    int resourceId = getNextResourceId(bossManifest);

    auto makeOptdat2Entry = [&](int startId) {
        int id = startId;
        return json::object_t{
                {"open", true},
                {"files", {
                        {md5FestivalByaml, {
                                {"id", id++},
                                {"type", "AppData"},
                                {"filename", "Festival.byaml"},
                                {"size", encryptedFestivalByamlData.size()},
                                {"notify", {
                                        {"new", "app"},
                                        {"LED", false}
                                }}
                        }},
                        {md5PanelTextureBFRES, {
                                {"id", id++},
                                {"type", "AppData"},
                                {"filename", "PanelTexture.bfres"},
                                {"size", encryptedPanelTextureBFRESData.size()},
                                {"notify", {
                                        {"new", "app"},
                                        {"LED", false}
                                }}
                        }},
                        {md5HapTextureBFRES, {
                                {"id", id},
                                {"type", "AppData"},
                                {"filename", "HapTexture.bfres"},
                                {"size", encryptedHapTextureBFRESData.size()},
                                {"notify", {
                                        {"new", "app"},
                                        {"LED", false}
                                }}
                        }}
                }}
        };
    };

    bossManifest["tasksheets"][EU_BOSS_APP_ID]["tasksheets"]["optdat2"] = makeOptdat2Entry(resourceId);
    resourceId += 3;
    bossManifest["tasksheets"][US_BOSS_APP_ID]["tasksheets"]["optdat2"] = makeOptdat2Entry(resourceId);
    resourceId += 3;
    bossManifest["tasksheets"][JP_BOSS_APP_ID]["tasksheets"]["optdat2"] = makeOptdat2Entry(resourceId);
}

async::Task<void> createVSSetting(std::chrono::system_clock::time_point afterFesBonusStartTime, json& bossManifest,
                                  const std::shared_ptr<db::Database>& db) {
    byaml::Byaml vsSettingByaml = generateVSSettingByaml(afterFesBonusStartTime);

    std::vector<uint8_t> vsSettingByamlData = vsSettingByaml.serialize();

    // Encrypt the data
    std::vector<uint8_t> encryptedVSSettingByamlData = crypto::encryptBOSS(vsSettingByamlData);

    // Calculate the hash
    std::string md5VSSettingByaml = util::bin2hex(crypto::MD5(encryptedVSSettingByamlData));

    auto saveCmd = db::Database::craftInsertOrUpdateFileCommand(md5VSSettingByaml, encryptedVSSettingByamlData);
    db::Result results = co_await db->runCommand(std::move(saveCmd));
    if (results.getStatus() != db::DBResultStatus::SUCCESS) {
        throw std::runtime_error("Database error while saving default VSSetting byaml for BOSS.");
    }

    int resourceId = getNextResourceId(bossManifest);

    auto makeSchdat2Entry = [&](int id) {
        return json::object_t{
                {"open", true},
                {"files", {
                        {md5VSSettingByaml, {
                                {"id", id},
                                {"type", "AppData"},
                                {"filename", "VSSetting.byaml"},
                                {"size", encryptedVSSettingByamlData.size()},
                                {"notify", {
                                        {"new", "app"},
                                        {"LED", false}
                                }}
                        }}
                }}
        };
    };

    bossManifest["tasksheets"][EU_BOSS_APP_ID]["tasksheets"]["schdat2"] = makeSchdat2Entry(resourceId++);
    bossManifest["tasksheets"][US_BOSS_APP_ID]["tasksheets"]["schdat2"] = makeSchdat2Entry(resourceId++);
    bossManifest["tasksheets"][JP_BOSS_APP_ID]["tasksheets"]["schdat2"] = makeSchdat2Entry(resourceId);
}



} // namespace boss