#include "bossManagement.hpp"
#include "management.hpp"

#include "../../util/util.hpp"
#include "../../db/database.hpp"
#include "../../crypto/tools.hpp"

#include <random>
#include <fstream>

namespace mgm {

using json = nlohmann::json;

static async::Task<json> readSetting(const std::shared_ptr<db::Database>& db, const std::string& key, const json& defaultVal) {
    auto cmd = db::Database::craftGetSettingCommand(key);
    auto result = co_await db->runCommand(std::move(cmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        co_return defaultVal;
    }
    try {
        co_return json::parse(result.getData<std::string>());
    } catch (...) {
        co_return defaultVal;
    }
}

static async::Task<void> writeSetting(const std::shared_ptr<db::Database>& db, const std::string& key, const json& value) {
    auto cmd = db::Database::craftInsertOrUpdateSettingCommand(key, value.dump());
    co_await db->runCommand(std::move(cmd));
}

static async::Task<void> queueBossTask(const std::shared_ptr<db::Database>& mgmtDb, int taskType, const std::string& params) {
    if (!mgmtDb) co_return;
    auto cmd = db::Database::craftInsertTaskCommand(taskType, params);
    co_await mgmtDb->runCommand(std::move(cmd));
}

static std::string loadDefaultImageBase64(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return "";
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return crypto::base64Encode(data);
}

static json generateDefaultFestival() {
    json f;
    f["id"] = 1000;
    f["battleResultRate"] = 1;
    f["lowPopulationNotJP"] = true;
    f["separateMatchingJP"] = true;
    f["gamemode"] = "turf_war";
    f["backupLanguage"] = "eu_en";
    f["stages"] = json::array({3, 11, 15});
    f["teamA"] = {
        {"color", {{"r", 12}, {"g", 24}, {"b", 48}, {"a", 255}}},
        {"names", {
            {"eu_en", "Team Alpha"}, {"eu_de", "Team Alpha"}, {"eu_es", "Team Alpha"},
            {"eu_fr", "Team Alpha"}, {"eu_it", "Team Alpha"}, {"jp", "チームA"},
            {"us_en", "Team Alpha"}, {"us_es", "Team Alpha"}, {"us_fr", "Team Alpha"}
        }},
        {"shortNames", {
            {"eu_en", "Alpha"}, {"eu_de", "Alpha"}, {"eu_es", "Alpha"},
            {"eu_fr", "Alpha"}, {"eu_it", "Alpha"}, {"jp", "A"},
            {"us_en", "Alpha"}, {"us_es", "Alpha"}, {"us_fr", "Alpha"}
        }},
        {"albedoImage", loadDefaultImageBase64("BodyTeamA.default.png")}
    };
    f["teamB"] = {
        {"color", {{"r", 48}, {"g", 24}, {"b", 12}, {"a", 255}}},
        {"names", {
            {"eu_en", "Team Bravo"}, {"eu_de", "Team Bravo"}, {"eu_es", "Team Bravo"},
            {"eu_fr", "Team Bravo"}, {"eu_it", "Team Bravo"}, {"jp", "チームB"},
            {"us_en", "Team Bravo"}, {"us_es", "Team Bravo"}, {"us_fr", "Team Bravo"}
        }},
        {"shortNames", {
            {"eu_en", "Bravo"}, {"eu_de", "Bravo"}, {"eu_es", "Bravo"},
            {"eu_fr", "Bravo"}, {"eu_it", "Bravo"}, {"jp", "B"},
            {"us_en", "Bravo"}, {"us_es", "Bravo"}, {"us_fr", "Bravo"}
        }},
        {"albedoImage", loadDefaultImageBase64("BodyTeamB.default.png")}
    };
    f["neutralColor"] = {{"r", 100}, {"g", 100}, {"b", 100}, {"a", 255}};

    auto now = std::chrono::system_clock::now();
    auto nowT = std::chrono::system_clock::to_time_t(now);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M", std::gmtime(&nowT));
    std::string nowStr = buf;

    f["announceTime"] = nowStr;
    f["startTime"] = nowStr;
    f["endTime"] = nowStr;
    f["resultTime"] = nowStr;
    f["afterFesBonusStart"] = nowStr;

    f["announceNews"] = json::object();
    f["startNews"] = json::object();
    f["resultANews"] = json::object();
    f["resultBNews"] = json::object();

    const std::vector<std::string> langs = {"eu_de", "eu_en", "eu_es", "eu_fr", "eu_it", "jp", "us_en", "us_es", "us_fr"};
    for (const auto& lang : langs) {
        json line = json::array({{
            {"command", "speak_raw_text"},
            {"emotion", "normal"},
            {"speaker", "callie"},
            {"text", "Default festival dialogue"},
            {"waitButton", false}
        }});
        f["announceNews"][lang] = line;
        f["startNews"][lang] = line;
        f["resultANews"][lang] = line;
        f["resultBNews"][lang] = line;
    }

    f["panelImage"] = loadDefaultImageBase64("PanelTexture.default.png");
    return f;
}

static json generateDefaultMapRotation() {
    json rotation;
    rotation["phases"] = json::array();

    std::vector<uint32_t> maps = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    std::vector<std::string> rules = {"cVar", "cVlf", "cVgl"};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> mapDist(0, static_cast<int>(maps.size()) - 1);
    std::uniform_int_distribution<> ruleDist(0, static_cast<int>(rules.size()) - 1);

    constexpr int PHASE_COUNT = 180;

    for (int i = 0; i < PHASE_COUNT; i++) {
        constexpr int PHASE_DURATION = 4;
        std::string gachiRule = rules[ruleDist(gen)];
        std::string regularRule = "cPnt";

        uint32_t gachiStage1 = maps[mapDist(gen)];
        uint32_t gachiStage2 = maps[mapDist(gen)];
        while (gachiStage2 == gachiStage1) gachiStage2 = maps[mapDist(gen)];

        uint32_t regularStage1 = maps[mapDist(gen)];
        uint32_t regularStage2 = maps[mapDist(gen)];
        while (regularStage2 == regularStage1) regularStage2 = maps[mapDist(gen)];

        int32_t duration = (i == PHASE_COUNT - 1) ? 87600 : PHASE_DURATION;

        json phase;
        phase["gachiRule"] = gachiRule;
        phase["regularRule"] = regularRule;
        phase["gachiStages"] = json::array({gachiStage1, gachiStage2});
        phase["regularStages"] = json::array({regularStage1, regularStage2});
        phase["duration"] = duration;

        rotation["phases"].push_back(phase);
    }

    return rotation;
}

static async::Task<void> ensureDefaultsExist(const std::shared_ptr<db::Database>& mgmDb, const std::shared_ptr<Logger::Logger>& logger) {
    json festivals = co_await readSetting(mgmDb, "festivals", json::array());
    if (festivals.empty()) {
        json defaultFes = generateDefaultFestival();
        festivals.push_back(defaultFes);
        co_await writeSetting(mgmDb, "festivals", festivals);
        co_await writeSetting(mgmDb, "active_festival_id", 1000);
        co_await queueBossTask(mgmDb, 1, std::to_string(1000));

        logger->log(Logger::level::INFO, Logger::group::SETUP, "No festivals found in database, created default festival with ID 1000.");
    }

    int activeId = (co_await readSetting(mgmDb, "active_festival_id", 0)).get<int>();
    bool hasActive = false;
    for (const auto& f : festivals) {
        if (f.value("id", 0) == activeId) { hasActive = true; break; }
    }
    if (!hasActive) {
        activeId = festivals[0].value("id", 1000);
        co_await writeSetting(mgmDb, "active_festival_id", activeId);
    }

    json rotation = co_await readSetting(mgmDb, "map_rotation", json::object());
    if (rotation.empty() || !rotation.contains("phases") || rotation["phases"].empty()) {
        rotation = generateDefaultMapRotation();
        co_await writeSetting(mgmDb, "map_rotation", rotation);
        auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        co_await writeSetting(mgmDb, "last_rotation_time", std::to_string(nowSec));
        co_await queueBossTask(mgmDb, 2, "");

        logger->log(Logger::level::INFO, Logger::group::SETUP, "No map rotation found in database, created default map rotation.");
    }
}

async::Task<void> initManagementData(const std::shared_ptr<db::Database>& mgmDb, const std::shared_ptr<Logger::Logger>& logger) {
    co_await ensureDefaultsExist(mgmDb, logger);
}

/*
 * GET /api/v1/festivals
 */
async::Task<void> mgm_get_festivals(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_GET && method != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }
    if (method == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, prepareCORSPreflightResponse(ctx, settingsMgr, "GET, POST, OPTIONS", keepAlive), keepAlive);
        co_return;
    }

    json festivals = co_await readSetting(mgmDb, "festivals", json::array());
    int activeId = (co_await readSetting(mgmDb, "active_festival_id", 0)).get<int>();

    // Strip heavy fields for the list view
    json summary = json::array();
    for (auto f : festivals) {
        json entry;
        entry["id"] = f.value("id", 0);
        entry["active"] = (f.value("id", 0) == activeId);

        // Pick backup language for team names
        std::string lang = f.value("backupLanguage", "us_en");
        if (f.contains("teamA") && f["teamA"].contains("names")) {
            entry["teamAName"] = f["teamA"]["names"].value(lang, "");
        }
        if (f.contains("teamB") && f["teamB"].contains("names")) {
            entry["teamBName"] = f["teamB"]["names"].value(lang, "");
        }
        summary.push_back(entry);
    }

    json responseBody;
    responseBody["festivals"] = summary;
    responseBody["activeId"] = activeId;

    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

/*
 * GET /api/v1/festivals/active
 */
async::Task<void> mgm_get_active_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_GET && method != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }
    if (method == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive), keepAlive);
        co_return;
    }

    int activeId = (co_await readSetting(mgmDb, "active_festival_id", 0)).get<int>();
    json festivals = co_await readSetting(mgmDb, "festivals", json::array());

    json activeFestival;
    for (const auto& fs : festivals) {
        if (fs.value("id", 0) == activeId) {
            activeFestival = fs;
            break;
        }
    }

    json responseBody;
    responseBody["activeId"] = activeId;
    responseBody["festival"] = activeFestival;

    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

/*
 * POST /api/v1/festivals
 */
async::Task<void> mgm_save_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_POST) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }

    json body;
    try {
        body = json::parse(std::string(ctx->request->getBody().begin(), ctx->request->getBody().end()));
    } catch (...) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::BAD_REQUEST, "Invalid JSON body",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST), false);
        co_return;
    }

    if (!body.contains("id")) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::BAD_REQUEST, "Missing festival ID",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST), false);
        co_return;
    }

    int festivalId = body["id"].get<int>();
    json festivals = co_await readSetting(mgmDb, "festivals", json::array());

    bool updated = false;
    for (auto& f : festivals) {
        if (f.value("id", 0) == festivalId) {
            f = body;
            updated = true;
            break;
        }
    }
    if (!updated) {
        festivals.push_back(body);
    }

    co_await writeSetting(mgmDb, "festivals", festivals);

    int activeId = (co_await readSetting(mgmDb, "active_festival_id", 0)).get<int>();
    if (activeId == festivalId) {
        // Only push festival update for the active one; VS setting too since afterFesBonusStart may have changed
        co_await queueBossTask(mgmDb, 1, std::to_string(festivalId));
        co_await queueBossTask(mgmDb, 2, "");
    }

    json responseBody = {{"status", "ok"}, {"id", festivalId}};
    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

/*
 * GET /api/v1/festivals/{id} — full details for editing
 */
async::Task<void> mgm_get_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb, int festivalId) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_GET && method != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }
    if (method == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, prepareCORSPreflightResponse(ctx, settingsMgr, "GET, OPTIONS", keepAlive), keepAlive);
        co_return;
    }

    json festivals = co_await readSetting(mgmDb, "festivals", json::array());
    for (const auto& f : festivals) {
        if (f.value("id", 0) == festivalId) {
            bool keepAlive = false;
            srv->sendResponse(ctx, prepareResponse(ctx, f, keepAlive,
                settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
            co_return;
        }
    }

    bool keepAlive = false;
    srv->sendResponse(ctx, createError(ctx, ManagementError::NOT_FOUND, "Festival not found",
        settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND), false);
    co_return;
}

/*
 * DELETE /api/v1/festivals/{id}
 */
async::Task<void> mgm_delete_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb, int festivalId) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_DELETE && method != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }
    if (method == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, prepareCORSPreflightResponse(ctx, settingsMgr, "DELETE, OPTIONS", keepAlive), keepAlive);
        co_return;
    }

    json festivals = co_await readSetting(mgmDb, "festivals", json::array());
    int activeId = (co_await readSetting(mgmDb, "active_festival_id", 0)).get<int>();

    if (festivalId == activeId) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::CONFLICT, "Cannot delete the active festival",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_CONFLICT), false);
        co_return;
    }

    json newFestivals = json::array();
    for (const auto& f : festivals) {
        if (f.value("id", 0) != festivalId) {
            newFestivals.push_back(f);
        }
    }

    if (newFestivals.size() == festivals.size()) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::NOT_FOUND, "Festival not found",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND), false);
        co_return;
    }

    co_await writeSetting(mgmDb, "festivals", newFestivals);

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

/*
 * POST /api/v1/festivals/switch
 */
async::Task<void> mgm_switch_active_festival(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_POST && method != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }
    if (method == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive), keepAlive);
        co_return;
    }

    json body;
    try {
        body = json::parse(std::string(ctx->request->getBody().begin(), ctx->request->getBody().end()));
    } catch (...) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::BAD_REQUEST, "Invalid JSON body",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST), false);
        co_return;
    }

    int newId = body.value("id", 0);
    if (newId == 0) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::BAD_REQUEST, "Missing id field",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST), false);
        co_return;
    }

    json festivals = co_await readSetting(mgmDb, "festivals", json::array());

    bool found = false;
    for (const auto& f : festivals) {
        if (f.value("id", 0) == newId) { found = true; break; }
    }

    if (!found) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::NOT_FOUND, "Festival not found",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_NOT_FOUND), false);
        co_return;
    }

    co_await writeSetting(mgmDb, "active_festival_id", newId);

    co_await queueBossTask(mgmDb, 1, std::to_string(newId));
    co_await queueBossTask(mgmDb, 2, "");

    json responseBody = {{"status", "ok"}, {"activeId", newId}};
    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

/*
 * GET /api/v1/map-rotation
 */
async::Task<void> mgm_get_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_GET && method != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }
    if (method == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, prepareCORSPreflightResponse(ctx, settingsMgr, "GET, PUT, OPTIONS", keepAlive), keepAlive);
        co_return;
    }

    json rotation = co_await readSetting(mgmDb, "map_rotation", generateDefaultMapRotation());
    int activeId = (co_await readSetting(mgmDb, "active_festival_id", 0)).get<int>();
    json festivals = co_await readSetting(mgmDb, "festivals", json::array());

    std::string afterFesBonusStart;
    for (const auto& f : festivals) {
        if (f.value("id", 0) == activeId) {
            afterFesBonusStart = f.value("afterFesBonusStart", "");
            break;
        }
    }

    json responseBody;
    responseBody["rotation"] = rotation;
    responseBody["afterFesBonusStart"] = afterFesBonusStart;

    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

/*
 * PUT /api/v1/map-rotation
 */
async::Task<void> mgm_update_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_PUT) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }

    json body;
    try {
        body = json::parse(std::string(ctx->request->getBody().begin(), ctx->request->getBody().end()));
    } catch (...) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::BAD_REQUEST, "Invalid JSON body",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST), false);
        co_return;
    }

    if (!body.contains("rotation")) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::BAD_REQUEST, "Missing rotation field",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_BAD_REQUEST), false);
        co_return;
    }

    co_await writeSetting(mgmDb, "map_rotation", body["rotation"]);
    co_await queueBossTask(mgmDb, 2, "");

    json responseBody = {{"status", "ok"}};
    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

/*
 * POST /api/v1/map-rotation/randomize
 */
async::Task<void> mgm_randomize_map_rotation(http::Server* srv, std::shared_ptr<http::Context> ctx, std::shared_ptr<SettingsManager> settingsMgr, std::shared_ptr<db::Database> mgmDb) {
    auto method = ctx->request->getMethod();
    if (method != http::Method::M_POST && method != http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, createError(ctx, ManagementError::METHOD_NOT_ALLOWED, "Method Not Allowed",
            settingsMgr->getManagementCORSAllowedOrigin(), keepAlive, HTTP_STATUS_METHOD_NOT_ALLOWED), false);
        co_return;
    }
    if (method == http::Method::M_OPTIONS) {
        bool keepAlive = false;
        srv->sendResponse(ctx, prepareCORSPreflightResponse(ctx, settingsMgr, "POST, OPTIONS", keepAlive), keepAlive);
        co_return;
    }

    json rotation = generateDefaultMapRotation();
    co_await writeSetting(mgmDb, "map_rotation", rotation);

    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    co_await writeSetting(mgmDb, "last_rotation_time", std::to_string(nowSec));

    co_await queueBossTask(mgmDb, 2, "");

    json responseBody;
    responseBody["status"] = "ok";
    responseBody["rotation"] = rotation;

    bool keepAlive = false;
    srv->sendResponse(ctx, prepareResponse(ctx, responseBody, keepAlive,
        settingsMgr->getManagementCORSAllowedOrigin(), HTTP_STATUS_OK), keepAlive);
    co_return;
}

} // namespace mgm
