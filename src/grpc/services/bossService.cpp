#include "bossService.hpp"

#include <chrono>

#include "../../boss/utils.hpp"
#include "../../boss/byaml/FestivalGenerator.hpp"
#include "../../boss/byaml/VSSettingGenerator.hpp"
#include "../../util/task.hpp"
#include "../../util/util.hpp"
#include "../../crypto/tools.hpp"
#include "../../constants.hpp"

using namespace async;

namespace grpcimpl::boss_config::v1 {

boss::festival::Language protoToFestivalLanguage(Language lang) {
    switch (lang) {
        case Language::EUROPEAN_GERMAN: return boss::festival::Language::EUROPEAN_GERMAN;
        case Language::EUROPEAN_ENGLISH: return boss::festival::Language::EUROPEAN_ENGLISH;
        case Language::EUROPEAN_SPANISH: return boss::festival::Language::EUROPEAN_SPANISH;
        case Language::EUROPEAN_FRENCH: return boss::festival::Language::EUROPEAN_FRENCH;
        case Language::EUROPEAN_ITALIAN: return boss::festival::Language::EUROPEAN_ITALIAN;
        case Language::JAPANESE: return boss::festival::Language::JAPANESE;
        case Language::AMERICAN_ENGLISH: return boss::festival::Language::AMERICAN_ENGLISH;
        case Language::AMERICAN_SPANISH: return boss::festival::Language::AMERICAN_SPANISH;
        case Language::AMERICAN_FRENCH: return boss::festival::Language::AMERICAN_FRENCH;
        default: return boss::festival::Language::AMERICAN_ENGLISH;
    }
}

boss::festival::Speaker protoToFestivalSpeaker(DialogueLine::Speaker speaker) {
    switch (speaker) {
        case DialogueLine::IDOL_LEFT: return boss::festival::Speaker::IDOL_LEFT;
        case DialogueLine::IDOL_RIGHT: return boss::festival::Speaker::IDOL_RIGHT;
        case DialogueLine::IDOL_ALL: return boss::festival::Speaker::IDOL_ALL;
        default: return boss::festival::Speaker::IDOL_LEFT;
    }
}

boss::festival::Emotion protoToFestivalEmotion(DialogueLine::Emotion emotion) {
    switch (emotion) {
        case DialogueLine::NORMAL_TALK: return boss::festival::Emotion::NORMAL_TALK;
        case DialogueLine::GREETING: return boss::festival::Emotion::GREETING;
        case DialogueLine::HAPPY: return boss::festival::Emotion::HAPPY;
        case DialogueLine::ANGRY: return boss::festival::Emotion::ANGRY;
        case DialogueLine::SURPRISED: return boss::festival::Emotion::SURPRISED;
        case DialogueLine::BORED: return boss::festival::Emotion::BORED;
        case DialogueLine::FEED: return boss::festival::Emotion::FEED;
        default: return boss::festival::Emotion::NORMAL_TALK;
    }
}

boss::festival::Gamemode protoToFestivalGamemode(Gamemode mode) {
    switch (mode) {
        case Gamemode::TURF_WAR: return boss::festival::Gamemode::TURF_WAR;
        case Gamemode::SPLAT_ZONES: return boss::festival::Gamemode::SPLAT_ZONES;
        case Gamemode::TOWER_CONTROL: return boss::festival::Gamemode::TOWER_CONTROL;
        case Gamemode::RAINMAKER: return boss::festival::Gamemode::RAINMAKER;
        default: return boss::festival::Gamemode::TURF_WAR;
    }
}

boss::festival::Stage protoToFestivalStage(Stage stage) {
    switch (stage) {
        case Stage::URCHIN_UNDERPASS: return boss::festival::Stage::URCHIN_UNDERPASS;
        case Stage::WALLEYE_WAREHOUSE: return boss::festival::Stage::WALLEYE_WAREHOUSE;
        case Stage::SALTSPRAY_RIG: return boss::festival::Stage::SALTSPRAY_RIG;
        case Stage::AROWANA_MALL: return boss::festival::Stage::AROWANA_MALL;
        case Stage::BLACKBELLY_SKATEPARK: return boss::festival::Stage::BLACKBELLY_SKATEPARK;
        case Stage::CAMP_TRIGGERFISH: return boss::festival::Stage::CAMP_TRIGGERFISH;
        case Stage::PORT_MACKEREL: return boss::festival::Stage::PORT_MACKEREL;
        case Stage::KELP_DOME: return boss::festival::Stage::KELP_DOME;
        case Stage::MORAY_TOWERS: return boss::festival::Stage::MORAY_TOWERS;
        case Stage::BLUEFIN_DEPOT: return boss::festival::Stage::BLUEFIN_DEPOT;
        case Stage::HAMMERHEAD_BRIDGE: return boss::festival::Stage::HAMMERHEAD_BRIDGE;
        case Stage::FLOUNDER_HEIGHTS: return boss::festival::Stage::FLOUNDER_HEIGHTS;
        case Stage::MUSEUM_D_ALFONSINO: return boss::festival::Stage::MUSEUM_D_ALFONSINO;
        case Stage::ANCHO_V_GAMES: return boss::festival::Stage::ANCHO_V_GAMES;
        case Stage::PIRANHA_PIT: return boss::festival::Stage::PIRANHA_PIT;
        case Stage::MAHIMAHI_RESORT: return boss::festival::Stage::MAHIMAHI_RESORT;
        default: return boss::festival::Stage::URCHIN_UNDERPASS;
    }
}

boss::festival::Color protoToFestivalColor(const Color& color) {
    return {
        static_cast<uint8_t>(color.r()),
        static_cast<uint8_t>(color.g()),
        static_cast<uint8_t>(color.b()),
        static_cast<uint8_t>(color.a())
    };
}

boss::festival::Dialogue protoToFestivalDialogue(const google::protobuf::RepeatedPtrField<LanguageDialogue>& langDialogues) {
    boss::festival::Dialogue dialogue;
    for (const auto& langDialogue : langDialogues) {
        boss::festival::Language lang = protoToFestivalLanguage(langDialogue.language());
        std::vector<boss::festival::DialogueLine> lines;
        for (const auto& line : langDialogue.lines()) {
            lines.push_back({
                boss::festival::Command::SPEAK_RAW_TEXT,
                protoToFestivalEmotion(line.emotion()),
                protoToFestivalSpeaker(line.speaker()),
                line.text(),
                line.wait_button()
            });
        }
        dialogue.lines[lang] = std::move(lines);
    }
    return dialogue;
}

boss::festival::TeamInfo protoToFestivalTeamInfo(const TeamInfo& teamInfo) {
    boss::festival::TeamInfo info;
    info.color = protoToFestivalColor(teamInfo.color());
    for (const auto& pair : teamInfo.names()) {
        info.names[protoToFestivalLanguage(pair.language())] = pair.value();
    }
    for (const auto& pair : teamInfo.short_names()) {
        info.shortNames[protoToFestivalLanguage(pair.language())] = pair.value();
    }
    return info;
}

std::chrono::system_clock::time_point protoToTimePoint(const google::protobuf::Timestamp& timestamp) {
    return std::chrono::system_clock::from_time_t(timestamp.seconds())
           + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(timestamp.nanos()));
}

boss::VSSettingFullConfig protoToVSSettingConfig(const VSSettingConfig& config) {
    boss::VSSettingFullConfig result;
    result.addFirstMatchingTime = config.add_first_matching_time();
    result.addMatchingTime = config.add_matching_time();
    if (config.has_after_fes_bonus_start()) {
        result.afterFesBonusStart = protoToTimePoint(config.after_fes_bonus_start());
    } else {
        result.afterFesBonusStart = std::chrono::system_clock::now();
    }
    result.bottleneckThresholdFrame = config.bottleneck_threshold_frame();
    result.disconnectByMemoryHash = config.disconnect_by_memory_hash();
    if (config.has_datetime()) {
        result.datetime = protoToTimePoint(config.datetime());
    } else {
        result.datetime = std::chrono::system_clock::now();
    }

    for (const auto& entry : config.map_first_appear()) {
        boss::MapFirstAppearance mfa;
        mfa.mapId = entry.map_id();
        if (entry.has_date()) {
            mfa.date = protoToTimePoint(entry.date());
        }
        result.mapFirstAppear.push_back(std::move(mfa));
    }

    for (const auto& phase : config.phases()) {
        boss::PhaseConfig pc;
        pc.gachiRule = protoToFestivalGamemode(phase.gachi_rule());
        pc.regularRule = protoToFestivalGamemode(phase.regular_rule());
        pc.gachiStages.assign(phase.gachi_stages().begin(), phase.gachi_stages().end());
        pc.regularStages.assign(phase.regular_stages().begin(), phase.regular_stages().end());
        pc.duration = phase.duration();
        result.phases.push_back(std::move(pc));
    }

    for (const auto& entry : config.rule_first_appear()) {
        boss::RuleFirstAppearance rfa;
        rfa.gachiRule = protoToFestivalGamemode(entry.gachi_rule());
        if (entry.has_date()) {
            rfa.date = protoToTimePoint(entry.date());
        }
        result.ruleFirstAppear.push_back(std::move(rfa));
    }

    result.timeoutAfterJoin = config.timeout_after_join();
    result.version = config.version();
    result.waitMatchingTime = config.wait_matching_time();

    for (const auto& entry : config.weapon_unlock()) {
        boss::WeaponUnlockEntry wue;
        wue.weaponSetId = entry.weapon_set_id();
        if (entry.has_date()) {
            wue.date = protoToTimePoint(entry.date());
        }
        result.weaponUnlock.push_back(std::move(wue));
    }

    result.webPost = config.web_post();
    return result;
}

Task<void> completeSetFestival(grpc::ServerUnaryReactor* reactor,
                               const SetFestivalRequest* request,
                               const std::shared_ptr<db::Database> db,
                               const std::shared_ptr<Logger::Logger> logger,
                               std::shared_ptr<http::Server> httpServer) {
    try {
        auto manifest = co_await boss::getManifest(logger, db);

        boss::festival::FestivalInfo festivalInfo;
        festivalInfo.id = request->id();
        festivalInfo.battleResultRate = request->battle_result_rate();
        festivalInfo.lowPopulationNotJP = request->low_population_not_jp();
        festivalInfo.separateMatchingJP = request->separate_matching_jp();
        festivalInfo.announceNews = protoToFestivalDialogue(request->announce_news());
        festivalInfo.startNews = protoToFestivalDialogue(request->start_news());
        festivalInfo.resultANews = protoToFestivalDialogue(request->result_a_news());
        festivalInfo.resultBNews = protoToFestivalDialogue(request->result_b_news());
        festivalInfo.gamemode = protoToFestivalGamemode(request->gamemode());

        std::array<boss::festival::Stage, 3> stages = {boss::festival::Stage::URCHIN_UNDERPASS,
                                                          boss::festival::Stage::URCHIN_UNDERPASS,
                                                          boss::festival::Stage::URCHIN_UNDERPASS};
        for (int i = 0; i < request->stages_size() && i < 3; ++i) {
            stages[i] = protoToFestivalStage(request->stages(i));
        }
        festivalInfo.stages = stages;

        if (request->has_team_a()) {
            festivalInfo.teamA = protoToFestivalTeamInfo(request->team_a());
        }
        if (request->has_team_b()) {
            festivalInfo.teamB = protoToFestivalTeamInfo(request->team_b());
        }
        if (request->has_neutral_color()) {
            festivalInfo.neutralColor = protoToFestivalColor(request->neutral_color());
        }

        if (request->has_announce_time()) {
            festivalInfo.announceTime = protoToTimePoint(request->announce_time());
        }
        if (request->has_start_time()) {
            festivalInfo.startTime = protoToTimePoint(request->start_time());
        }
        if (request->has_end_time()) {
            festivalInfo.endTime = protoToTimePoint(request->end_time());
        }
        if (request->has_result_time()) {
            festivalInfo.resultTime = protoToTimePoint(request->result_time());
        }
        if (request->has_after_fes_bonus_start()) {
            festivalInfo.afterFesBonusStart = protoToTimePoint(request->after_fes_bonus_start());
        }

        festivalInfo.backupLanguage = protoToFestivalLanguage(request->backup_language());

        std::vector<uint8_t> bodyTeamA(request->body_team_a().begin(), request->body_team_a().end());
        std::vector<uint8_t> bodyTeamB(request->body_team_b().begin(), request->body_team_b().end());
        std::vector<uint8_t> panelTexture(request->panel_texture().begin(), request->panel_texture().end());

        std::shared_ptr<db::Database> session = db->createSession();
        if ((co_await session->startTransaction(true)).getStatus() != db::DBResultStatus::SUCCESS) {
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error starting transaction"));
            co_return;
        }

        std::string exceptionMsg;
        try {
            co_await boss::createFestival(festivalInfo, bodyTeamA, bodyTeamB, panelTexture, manifest, session);
        } catch (const std::exception& e) {
            exceptionMsg = e.what();
        }

        if (!exceptionMsg.empty()) {
            co_await session->rollbackTransaction();
            logger->log(Logger::level::FAILURE, Logger::group::GRPC,
                        "Failed to create festival: " + exceptionMsg);
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, exceptionMsg));
            co_return;
        }

        auto saveManifestCmd = db::Database::craftInsertOrUpdateSettingCommand("manifest", manifest.dump());
        auto results = co_await session->runCommand(std::move(saveManifestCmd));
        if (results.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error saving manifest"));
            co_return;
        }

        if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error committing transaction"));
            co_return;
        }

        reactor->Finish(grpc::Status::OK);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::GRPC,
                    "SetFestival error: " + std::string(e.what()));
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
    }
}

Task<void> completeSetVSSetting(grpc::ServerUnaryReactor* reactor,
                                const SetVSSettingRequest* request,
                                const std::shared_ptr<db::Database> db,
                                const std::shared_ptr<Logger::Logger> logger,
                                std::shared_ptr<http::Server> httpServer) {
    try {
        const auto& config = request->config();

        auto manifest = co_await boss::getManifest(logger, db);

        boss::VSSettingFullConfig vsConfig = protoToVSSettingConfig(request->config());

        boss::byaml::Byaml vsSettingByaml = boss::generateVSSettingByamlFromConfig(vsConfig);
        std::vector<uint8_t> vsSettingByamlData = vsSettingByaml.serialize();

        std::vector<uint8_t> encryptedVSSettingByamlData = crypto::encryptBOSS(vsSettingByamlData);
        std::string md5VSSettingByaml = util::bin2hex(crypto::MD5(encryptedVSSettingByamlData));

        std::shared_ptr<db::Database> session = db->createSession();
        if ((co_await session->startTransaction(true)).getStatus() != db::DBResultStatus::SUCCESS) {
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error starting transaction"));
            co_return;
        }

        auto saveCmd = db::Database::craftInsertOrUpdateFileCommand(md5VSSettingByaml, encryptedVSSettingByamlData);
        auto results = co_await session->runCommand(std::move(saveCmd));
        if (results.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error saving VS setting file"));
            co_return;
        }

        int resourceId = 1000;
        for (const auto& [key, value] : manifest["tasksheets"].items()) {
            for (const auto& [tsKey, tsVal] : value["tasksheets"].items()) {
                for (const auto& [fileKey, fileVal] : tsVal["files"].items()) {
                    resourceId = std::max(resourceId, fileVal["id"].get<int>());
                }
            }
        }

        auto makeSchdat2Entry = [&](int id) {
            json::object_t schdat2 = {
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
            return schdat2;
        };

        manifest["tasksheets"][EU_BOSS_APP_ID]["tasksheets"]["schdat2"] = makeSchdat2Entry(++resourceId);
        manifest["tasksheets"][US_BOSS_APP_ID]["tasksheets"]["schdat2"] = makeSchdat2Entry(++resourceId);
        manifest["tasksheets"][JP_BOSS_APP_ID]["tasksheets"]["schdat2"] = makeSchdat2Entry(++resourceId);

        auto saveManifestCmd = db::Database::craftInsertOrUpdateSettingCommand("manifest", manifest.dump());
        results = co_await session->runCommand(std::move(saveManifestCmd));
        if (results.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error saving manifest"));
            co_return;
        }

        if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error committing transaction"));
            co_return;
        }

        reactor->Finish(grpc::Status::OK);
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, Logger::group::GRPC,
                    "SetVSSetting error: " + std::string(e.what()));
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
    }
}

grpc::ServerUnaryReactor* BossServiceImpl::SetFestival(grpc::CallbackServerContext* context,
                                                        const SetFestivalRequest* request,
                                                        google::protobuf::Empty* _) {
    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(BossService::service_full_name()) + "] SetFestival called "
               "for Festival ID: " + std::to_string(request->id()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeSetFestival(reactor, request, db, logger, httpServer);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

grpc::ServerUnaryReactor* BossServiceImpl::SetVSSetting(grpc::CallbackServerContext* context,
                                                         const SetVSSettingRequest* request,
                                                         google::protobuf::Empty* _) {
    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(BossService::service_full_name()) + "] SetVSSetting called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeSetVSSetting(reactor, request, db, logger, httpServer);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

} // namespace grpcimpl::boss_config::v1
