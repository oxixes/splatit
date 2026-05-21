#include "globalTaskScheduler.hpp"

#include "../db/database.hpp"
#include "../http/account/account.hpp"
#include "../grpc/asyncRequest.hpp"
#include "../grpc/channelPool.hpp"
#include "../settingsManager.hpp"

#include <boss.grpc.pb.h>
#include <google/protobuf/empty.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <random>
#include <iomanip>
#include <sstream>

#include "../crypto/tools.hpp"

namespace util {

std::shared_ptr<GlobalTaskScheduler> globalTaskSchedulerInstance = nullptr;

void GlobalTaskScheduler::createInstance(std::shared_ptr<db::Database> accountsDb, std::shared_ptr<Logger::Logger> logger,
    std::shared_ptr<SettingsManager> settingsManager, std::shared_ptr<db::Database> mgmtDb,
    std::shared_ptr<grpcimpl::ChannelPool> channelPool) {
    if (globalTaskSchedulerInstance != nullptr) {
        throw std::runtime_error("GlobalTaskScheduler instance already created");
    }

    globalTaskSchedulerInstance = std::shared_ptr<GlobalTaskScheduler>(new GlobalTaskScheduler(std::move(accountsDb),
        std::move(logger), std::move(settingsManager), std::move(mgmtDb), std::move(channelPool)));
    globalTaskSchedulerInstance->startScheduler();
}

GlobalTaskScheduler& GlobalTaskScheduler::getInstance() {
    if (globalTaskSchedulerInstance == nullptr) {
        throw std::runtime_error("GlobalTaskScheduler instance not created yet");
    }

    return *globalTaskSchedulerInstance;
}

uint64_t GlobalTaskScheduler::process() {
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> currentTime =
        std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());

    auto elapsed = currentTime - lastProcessTime;
    auto interval = std::chrono::milliseconds(GLOBAL_TASK_PROCESS_INTERVAL_SECONDS * 1000);

    if (elapsed < interval) {
        // Return remaining milliseconds until next process
        return std::chrono::duration_cast<std::chrono::milliseconds>(interval - elapsed).count();
    }

    lastProcessTime = currentTime;

    // TODO Maybe improve this so it gets the time from the last updated time in the database
    // Check for daily map rotation (every 24 hours)
    auto nowHours = std::chrono::time_point_cast<std::chrono::hours>(std::chrono::system_clock::now());
    auto hoursSinceLastRotation = nowHours - lastDailyRotationTime;
    if (hoursSinceLastRotation >= std::chrono::hours(24) && mgmtDb != nullptr && settingsManager->isManagementEnabled()) {
        lastDailyRotationTime = nowHours;
        schedule(std::move(performDailyMapRotation()));
    }

    schedule(std::move(internalProcess()));

    return GLOBAL_TASK_PROCESS_INTERVAL_SECONDS * 1000;
}

void GlobalTaskScheduler::schedule(async::Task<void>&& task) const {
    task.setScheduler(scheduler);
    scheduler->schedule(std::move(task));
}

void GlobalTaskScheduler::stop() {
    shouldStop = true;
    schedulerCV->notify_one();
    if (schedulerThread.joinable()) {
        schedulerThread.join();
    }
}

void GlobalTaskScheduler::schedulerThreadFunc() {
    while (!shouldStop) {
        std::unique_lock lock(schedulerMutex);
        schedulerCV->wait(lock, [this] { return scheduler->hasTasks() || shouldStop; });

        if (shouldStop) break;

        lock.unlock();

        while (scheduler->hasTasks() && !shouldStop) {
            auto task = scheduler->getTask();
            if (!task) break;

            try {
                async::Scheduler::run(task);
            } catch (const std::exception& e) {
                logger->log(Logger::level::FAILURE, Logger::group::GLOBAL_TASKS, "An exception occurred while running a task: " + std::string(e.what()));
            }
        }
    }
}

void GlobalTaskScheduler::startScheduler() {
    schedulerThread = std::thread(&GlobalTaskScheduler::schedulerThreadFunc, this);
}

async::Task<void> GlobalTaskScheduler::internalProcess() const {
    logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS, "Processing global tasks...");

    uint32_t processedTasks = 0;

    // Process account-level tasks (DELETE_FRIENDS_ACCOUNT)
    if (accountsDb != nullptr) {
        auto getAccTasksCmd = db::Database::craftGetAllTasksCommand();
        const auto getAccTasksResult = co_await accountsDb->runCommand(std::move(getAccTasksCmd));
        if (getAccTasksResult.getStatus() == db::DBResultStatus::SUCCESS) {
            const auto tasks = getAccTasksResult.getData<std::vector<db::DBTaskData>>();
            for (const auto& taskData : tasks) {
                logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS,
                            "Processing accounts task ID " + std::to_string(taskData.id) +
                            " of type " + std::to_string(taskData.type));

                switch (static_cast<GlobalTaskType>(taskData.type)) {
                case GlobalTaskType::DELETE_FRIENDS_ACCOUNT: {
                    const uint32_t pid = std::stoul(taskData.params);
                    logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS,
                                "Deleting friends secure server accounts for PID " + std::to_string(pid));
                    if (co_await acc::deleteFriendsServerAccountForAccount(pid, settingsManager, logger)) {
                        logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS,
                                    "Successfully deleted friends secure server accounts for PID " + std::to_string(pid));
                        co_await deleteTask(taskData.id);
                    }
                    break;
                }
                default: break;
                }
                processedTasks++;
            }
        }
    }

    // Process management-level tasks
    if (mgmtDb != nullptr && settingsManager->isManagementEnabled()) {
        auto getMgmtTasksCmd = db::Database::craftGetAllTasksCommand();
        const auto getMgmtTasksResult = co_await mgmtDb->runCommand(std::move(getMgmtTasksCmd));
        if (getMgmtTasksResult.getStatus() == db::DBResultStatus::SUCCESS) {
            const auto tasks = getMgmtTasksResult.getData<std::vector<db::DBTaskData>>();
            for (const auto& taskData : tasks) {
                logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS,
                            "Processing management task ID " + std::to_string(taskData.id) +
                            " of type " + std::to_string(taskData.type));

                switch (static_cast<GlobalTaskType>(taskData.type)) {
                    case GlobalTaskType::BOSS_UPDATE_FESTIVAL: {
                        const int festivalId = std::stoi(taskData.params);
                        logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS,
                                    "Updating boss festival ID " + std::to_string(festivalId));
                        if (co_await processBossFestivalUpdate(festivalId)) {
                            logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS,
                                        "Successfully updated boss festival ID " + std::to_string(festivalId));
                            co_await deleteMgmtTask(taskData.id);
                        } else {
                            logger->log(Logger::level::WARN, Logger::group::GLOBAL_TASKS,
                                        "Failed to update boss festival ID " + std::to_string(festivalId) + ", will retry");
                        }
                        break;
                    }
                    case GlobalTaskType::BOSS_UPDATE_VS_SETTING: {
                        logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS, "Updating boss VS setting");
                        if (co_await processBossVSSettingUpdate()) {
                            logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS, "Successfully updated boss VS setting");
                            co_await deleteMgmtTask(taskData.id);
                        } else {
                            logger->log(Logger::level::WARN, Logger::group::GLOBAL_TASKS,
                                        "Failed to update boss VS setting, will retry");
                        }
                        break;
                    }
                    default: break;
                }
                processedTasks++;
            }
        }
    }

    logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS,
                 "Finished processing global tasks. Processed " + std::to_string(processedTasks) + " tasks.");

    co_return;
}

async::Task<void> GlobalTaskScheduler::deleteTask(const int64_t id) const {
    auto deleteTaskCmd = db::Database::craftDeleteTaskCommand(id);
    const auto deleteTaskResult = co_await accountsDb->runCommand(std::move(deleteTaskCmd));
    if (deleteTaskResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::FAILURE, Logger::group::GLOBAL_TASKS,
                    "Failed to delete task ID " + std::to_string(id) + " from accounts database");
    } else {
        logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS,
                    "Deleted task ID " + std::to_string(id) + " from accounts database");
    }
}

async::Task<void> GlobalTaskScheduler::deleteMgmtTask(const int64_t id) const {
    if (!mgmtDb) co_return;
    auto deleteTaskCmd = db::Database::craftDeleteTaskCommand(id);
    const auto deleteTaskResult = co_await mgmtDb->runCommand(std::move(deleteTaskCmd));
    if (deleteTaskResult.getStatus() != db::DBResultStatus::SUCCESS) {
        logger->log(Logger::level::FAILURE, Logger::group::GLOBAL_TASKS,
                    "Failed to delete management task ID " + std::to_string(id));
    } else {
        logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS,
                    "Deleted management task ID " + std::to_string(id));
    }
}

async::Task<bool> GlobalTaskScheduler::processBossFestivalUpdate(int festivalId) const {
    if (!mgmtDb || !channelPool) co_return false;

    auto getCmd = db::Database::craftGetSettingCommand("festivals");
    auto result = co_await mgmtDb->runCommand(std::move(getCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        co_return false;
    }
    auto festivalsJson = result.getData<std::string>();
    nlohmann::json festivals;
    try {
        festivals = nlohmann::json::parse(festivalsJson);
    } catch (...) {
        co_return false;
    }

    nlohmann::json targetFestival;
    for (const auto& f : festivals) {
        if (f.value("id", 0) == festivalId) {
            targetFestival = f;
            break;
        }
    }
    if (targetFestival.empty()) co_return false;

    auto bAddrs = settingsManager->getManagementServerAddresses();
    auto bit = bAddrs.find(ServerType::BOSS);
    std::vector<sock::IPv4Addr> bAddrList;
    if (bit != bAddrs.end() && !bit->second.empty()) {
        bAddrList = bit->second;
    }
    if (bAddrList.empty()) {
        bAddrList.push_back(settingsManager->getgRPCListenAddress());
    }
    if (bAddrList.empty()) co_return false;

    bool success = false;
    for (const auto& addr : bAddrList) {
        auto channel = channelPool->getChannel(util::ipv4WPortToString(addr));
        if (!channel) continue;

        auto stub = grpcimpl::boss_config::v1::BossService::NewStub(channel);
        auto request = std::make_shared<grpcimpl::boss_config::v1::SetFestivalRequest>();

        request->set_id(targetFestival.value("id", 0));
        request->set_battle_result_rate(targetFestival.value("battleResultRate", 1));
        request->set_low_population_not_jp(targetFestival.value("lowPopulationNotJP", true));
        request->set_separate_matching_jp(targetFestival.value("separateMatchingJP", true));

        std::string gamemode = targetFestival.value("gamemode", "turf_war");
        if (gamemode == "turf_war") request->set_gamemode(grpcimpl::boss_config::v1::TURF_WAR);
        else if (gamemode == "splat_zones") request->set_gamemode(grpcimpl::boss_config::v1::SPLAT_ZONES);
        else if (gamemode == "tower_control") request->set_gamemode(grpcimpl::boss_config::v1::TOWER_CONTROL);
        else request->set_gamemode(grpcimpl::boss_config::v1::RAINMAKER);

        std::string backupLang = targetFestival.value("backupLanguage", "eu_en");
        static const std::map<std::string, grpcimpl::boss_config::v1::Language> langMap = {
            {"eu_de", grpcimpl::boss_config::v1::EUROPEAN_GERMAN},
            {"eu_en", grpcimpl::boss_config::v1::EUROPEAN_ENGLISH},
            {"eu_es", grpcimpl::boss_config::v1::EUROPEAN_SPANISH},
            {"eu_fr", grpcimpl::boss_config::v1::EUROPEAN_FRENCH},
            {"eu_it", grpcimpl::boss_config::v1::EUROPEAN_ITALIAN},
            {"jp", grpcimpl::boss_config::v1::JAPANESE},
            {"us_en", grpcimpl::boss_config::v1::AMERICAN_ENGLISH},
            {"us_es", grpcimpl::boss_config::v1::AMERICAN_SPANISH},
            {"us_fr", grpcimpl::boss_config::v1::AMERICAN_FRENCH}
        };
        auto langIt = langMap.find(backupLang);
        if (langIt != langMap.end()) request->set_backup_language(langIt->second);

        if (targetFestival.contains("stages") && targetFestival["stages"].is_array()) {
            static const std::map<int, grpcimpl::boss_config::v1::Stage> stageMap = {
                {0, grpcimpl::boss_config::v1::URCHIN_UNDERPASS}, {1, grpcimpl::boss_config::v1::WALLEYE_WAREHOUSE},
                {2, grpcimpl::boss_config::v1::SALTSPRAY_RIG}, {3, grpcimpl::boss_config::v1::AROWANA_MALL},
                {4, grpcimpl::boss_config::v1::BLACKBELLY_SKATEPARK}, {5, grpcimpl::boss_config::v1::CAMP_TRIGGERFISH},
                {6, grpcimpl::boss_config::v1::PORT_MACKEREL}, {7, grpcimpl::boss_config::v1::KELP_DOME},
                {8, grpcimpl::boss_config::v1::MORAY_TOWERS}, {9, grpcimpl::boss_config::v1::BLUEFIN_DEPOT},
                {10, grpcimpl::boss_config::v1::HAMMERHEAD_BRIDGE}, {11, grpcimpl::boss_config::v1::FLOUNDER_HEIGHTS},
                {12, grpcimpl::boss_config::v1::MUSEUM_D_ALFONSINO}, {13, grpcimpl::boss_config::v1::ANCHO_V_GAMES},
                {14, grpcimpl::boss_config::v1::PIRANHA_PIT}, {15, grpcimpl::boss_config::v1::MAHIMAHI_RESORT}
            };
            for (const auto& s : targetFestival["stages"]) {
                int sid = s.get<int>();
                auto sit = stageMap.find(sid);
                if (sit != stageMap.end()) request->add_stages(sit->second);
            }
        }

        // Team A
        if (targetFestival.contains("teamA")) {
            auto* teamA = request->mutable_team_a();
            const auto& ta = targetFestival["teamA"];
            if (ta.contains("color")) {
                auto* color = teamA->mutable_color();
                color->set_r(ta["color"].value("r", 0));
                color->set_g(ta["color"].value("g", 0));
                color->set_b(ta["color"].value("b", 0));
                color->set_a(ta["color"].value("a", 255));
            }
            if (ta.contains("names")) {
                for (const auto& [lkey, lval] : ta["names"].items()) {
                    auto lit = langMap.find(lkey);
                    if (lit != langMap.end()) {
                        auto* pair = teamA->add_names();
                        pair->set_language(lit->second);
                        pair->set_value(lval.get<std::string>());
                    }
                }
            }
            if (ta.contains("shortNames")) {
                for (const auto& [lkey, lval] : ta["shortNames"].items()) {
                    auto lit = langMap.find(lkey);
                    if (lit != langMap.end()) {
                        auto* pair = teamA->add_short_names();
                        pair->set_language(lit->second);
                        pair->set_value(lval.get<std::string>());
                    }
                }
            }
        }

        // Team B
        if (targetFestival.contains("teamB")) {
            auto* teamB = request->mutable_team_b();
            const auto& tb = targetFestival["teamB"];
            if (tb.contains("color")) {
                auto* color = teamB->mutable_color();
                color->set_r(tb["color"].value("r", 0));
                color->set_g(tb["color"].value("g", 0));
                color->set_b(tb["color"].value("b", 0));
                color->set_a(tb["color"].value("a", 255));
            }
            if (tb.contains("names")) {
                for (const auto& [lkey, lval] : tb["names"].items()) {
                    auto lit = langMap.find(lkey);
                    if (lit != langMap.end()) {
                        auto* pair = teamB->add_names();
                        pair->set_language(lit->second);
                        pair->set_value(lval.get<std::string>());
                    }
                }
            }
            if (tb.contains("shortNames")) {
                for (const auto& [lkey, lval] : tb["shortNames"].items()) {
                    auto lit = langMap.find(lkey);
                    if (lit != langMap.end()) {
                        auto* pair = teamB->add_short_names();
                        pair->set_language(lit->second);
                        pair->set_value(lval.get<std::string>());
                    }
                }
            }
        }

        // Neutral color
        if (targetFestival.contains("neutralColor")) {
            auto* nc = request->mutable_neutral_color();
            nc->set_r(targetFestival["neutralColor"].value("r", 128));
            nc->set_g(targetFestival["neutralColor"].value("g", 128));
            nc->set_b(targetFestival["neutralColor"].value("b", 128));
            nc->set_a(targetFestival["neutralColor"].value("a", 255));
        }

        // Albedo textures and panel image
        if (targetFestival.contains("teamA") && targetFestival["teamA"].contains("albedoImage")) {
            std::string b64 = targetFestival["teamA"]["albedoImage"].get<std::string>();
            if (!b64.empty()) {
                auto decoded = crypto::base64Decode(b64);
                request->set_body_team_a(decoded.data(), decoded.size());
            }
        }
        if (targetFestival.contains("teamB") && targetFestival["teamB"].contains("albedoImage")) {
            std::string b64 = targetFestival["teamB"]["albedoImage"].get<std::string>();
            if (!b64.empty()) {
                auto decoded = crypto::base64Decode(b64);
                request->set_body_team_b(decoded.data(), decoded.size());
            }
        }
        if (targetFestival.contains("panelImage")) {
            std::string b64 = targetFestival["panelImage"].get<std::string>();
            if (!b64.empty()) {
                auto decoded = crypto::base64Decode(b64);
                request->set_panel_texture(decoded.data(), decoded.size());
            }
        }

        // Timestamps
        auto parseTime = [](const std::string& s) -> google::protobuf::Timestamp {
            google::protobuf::Timestamp ts;
            if (s.empty()) return ts;
            struct tm tm = {};
            std::string trimmed = s;
            if (trimmed.size() >= 16) {
                std::istringstream ss(trimmed);
                ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M");
                if (!ss.fail()) {
                    time_t t = timegm(&tm);
                    ts.set_seconds(t);
                }
            }
            return ts;
        };

        if (targetFestival.contains("announceTime")) *request->mutable_announce_time() = parseTime(targetFestival["announceTime"].get<std::string>());
        if (targetFestival.contains("startTime")) *request->mutable_start_time() = parseTime(targetFestival["startTime"].get<std::string>());
        if (targetFestival.contains("endTime")) *request->mutable_end_time() = parseTime(targetFestival["endTime"].get<std::string>());
        if (targetFestival.contains("resultTime")) *request->mutable_result_time() = parseTime(targetFestival["resultTime"].get<std::string>());
        if (targetFestival.contains("afterFesBonusStart")) *request->mutable_after_fes_bonus_start() = parseTime(targetFestival["afterFesBonusStart"].get<std::string>());

        // Convert dialogue
        auto convertDialogue = [&](const std::string& newsType, auto* repeatedField) {
            if (!targetFestival.contains(newsType)) return;
            const auto& news = targetFestival[newsType];
            for (const auto& [lkey, lval] : news.items()) {
                auto lit = langMap.find(lkey);
                if (lit == langMap.end()) continue;
                auto* langDialogue = repeatedField->Add();
                langDialogue->set_language(lit->second);
                for (const auto& line : lval) {
                    auto* dl = langDialogue->add_lines();
                    dl->set_command(grpcimpl::boss_config::v1::DialogueLine::SPEAK_RAW_TEXT);

                    std::string emotion = line.value("emotion", "normal");
                    if (emotion == "greeting") dl->set_emotion(grpcimpl::boss_config::v1::DialogueLine::GREETING);
                    else if (emotion == "happy") dl->set_emotion(grpcimpl::boss_config::v1::DialogueLine::HAPPY);
                    else if (emotion == "angry") dl->set_emotion(grpcimpl::boss_config::v1::DialogueLine::ANGRY);
                    else if (emotion == "surprised") dl->set_emotion(grpcimpl::boss_config::v1::DialogueLine::SURPRISED);
                    else if (emotion == "bored") dl->set_emotion(grpcimpl::boss_config::v1::DialogueLine::BORED);
                    else if (emotion == "feed") dl->set_emotion(grpcimpl::boss_config::v1::DialogueLine::FEED);
                    else dl->set_emotion(grpcimpl::boss_config::v1::DialogueLine::NORMAL_TALK);

                    std::string speaker = line.value("speaker", "callie");
                    if (speaker == "marie") dl->set_speaker(grpcimpl::boss_config::v1::DialogueLine::IDOL_RIGHT);
                    else if (speaker == "both") dl->set_speaker(grpcimpl::boss_config::v1::DialogueLine::IDOL_ALL);
                    else dl->set_speaker(grpcimpl::boss_config::v1::DialogueLine::IDOL_LEFT);

                    dl->set_text(line.value("text", ""));
                    dl->set_wait_button(line.value("waitButton", false));
                }
            }
        };

        convertDialogue("announceNews", request->mutable_announce_news());
        convertDialogue("startNews", request->mutable_start_news());
        convertDialogue("resultANews", request->mutable_result_a_news());
        convertDialogue("resultBNews", request->mutable_result_b_news());

        auto response = co_await grpcimpl::callAsync<
            grpcimpl::boss_config::v1::BossService::Stub,
            void (grpcimpl::boss_config::v1::BossService::Stub::async::*)(
                grpc::ClientContext*,
                const grpcimpl::boss_config::v1::SetFestivalRequest*,
                google::protobuf::Empty*,
                std::function<void(grpc::Status)>
            ),
            grpcimpl::boss_config::v1::SetFestivalRequest,
            google::protobuf::Empty
        >(
            stub,
            &grpcimpl::boss_config::v1::BossService::Stub::async::SetFestival,
            request,
            30000
        );

        if (response.second.ok()) {
            success = true;
            break;
        }
        logger->log(Logger::level::WARN, Logger::group::GLOBAL_TASKS,
                    "Boss update for festival " + std::to_string(festivalId) + " failed at " +
                    util::ipv4WPortToString(addr) + ": " + response.second.error_message());
    }

    co_return success;
}

async::Task<bool> GlobalTaskScheduler::processBossVSSettingUpdate() const {
    if (!mgmtDb || !channelPool) co_return false;

    // Read map rotation from boss DB
    auto getCmd = db::Database::craftGetSettingCommand("map_rotation");
    auto result = co_await mgmtDb->runCommand(std::move(getCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        co_return false;
    }
    nlohmann::json rotation;
    try {
        rotation = nlohmann::json::parse(result.getData<std::string>());
    } catch (...) {
        co_return false;
    }

    // Read active festival for afterFesBonusStart
    auto activeIdCmd = db::Database::craftGetSettingCommand("active_festival_id");
    auto activeIdResult = co_await mgmtDb->runCommand(std::move(activeIdCmd));
    int activeId = 0;
    if (activeIdResult.getStatus() == db::DBResultStatus::SUCCESS && activeIdResult.hasData()) {
        activeId = std::stoi(activeIdResult.getData<std::string>());
    }

    auto festivalsCmd = db::Database::craftGetSettingCommand("festivals");
    auto festivalsResult = co_await mgmtDb->runCommand(std::move(festivalsCmd));
    std::string afterFesBonusStartStr;
    if (festivalsResult.getStatus() == db::DBResultStatus::SUCCESS && festivalsResult.hasData()) {
        nlohmann::json festivals = nlohmann::json::parse(festivalsResult.getData<std::string>());
        for (const auto& f : festivals) {
            if (f.value("id", 0) == activeId) {
                afterFesBonusStartStr = f.value("afterFesBonusStart", "");
                break;
            }
        }
    }

    auto bossAddrs = settingsManager->getManagementServerAddresses();
    auto it = bossAddrs.find(ServerType::BOSS);
    std::vector<sock::IPv4Addr> addrList;
    if (it != bossAddrs.end() && !it->second.empty()) {
        addrList = it->second;
    }
    if (addrList.empty()) {
        addrList.push_back(settingsManager->getgRPCListenAddress());
    }
    if (addrList.empty()) co_return false;

    bool success = false;
    for (const auto& addr : addrList) {
        auto channel = channelPool->getChannel(util::ipv4WPortToString(addr));
        if (!channel) continue;

        auto stub = grpcimpl::boss_config::v1::BossService::NewStub(channel);
        auto request = std::make_shared<grpcimpl::boss_config::v1::SetVSSettingRequest>();
        auto* config = request->mutable_config();

        config->set_add_first_matching_time(30);
        config->set_add_matching_time(0);
        config->set_bottleneck_threshold_frame(480);
        config->set_disconnect_by_memory_hash(true);
        config->set_timeout_after_join(120);
        config->set_version(3);
        config->set_wait_matching_time(25);
        config->set_web_post(true);

        // Set datetime to now
        auto now = std::chrono::system_clock::now();
        auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        config->mutable_datetime()->set_seconds(nowSeconds);

        // afterFesBonusStart from active festival
        if (!afterFesBonusStartStr.empty() && afterFesBonusStartStr.size() >= 16) {
            struct tm tm = {};
            std::istringstream ss(afterFesBonusStartStr);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M");
            if (!ss.fail()) {
                config->mutable_after_fes_bonus_start()->set_seconds(timegm(&tm));
            } else {
                config->mutable_after_fes_bonus_start()->set_seconds(nowSeconds);
            }
        } else {
            config->mutable_after_fes_bonus_start()->set_seconds(nowSeconds);
        }

        // Map first appearances
        std::vector<uint32_t> allMaps = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        struct tm mapDateTm = {};
        std::istringstream("2015-04-17") >> std::get_time(&mapDateTm, "%Y-%m-%d");
        time_t mapDate = timegm(&mapDateTm);
        for (uint32_t m : allMaps) {
            auto* mfa = config->add_map_first_appear();
            mfa->set_map_id(m);
            mfa->mutable_date()->set_seconds(mapDate);
        }

        // Rule first appearances
        static const std::map<std::string, grpcimpl::boss_config::v1::Gamemode> ruleRevMap = {
            {"cVar", grpcimpl::boss_config::v1::SPLAT_ZONES},
            {"cVlf", grpcimpl::boss_config::v1::TOWER_CONTROL},
            {"cVgl", grpcimpl::boss_config::v1::RAINMAKER}
        };
        for (const auto& [ruleStr, gm] : ruleRevMap) {
            auto* rfa = config->add_rule_first_appear();
            rfa->set_gachi_rule(gm);
            rfa->mutable_date()->set_seconds(mapDate);
        }

        // Weapon unlock
        std::vector<uint32_t> weaponSets = {
            1000, 1001, 1002, 1020, 1021, 1032, 1042, 1060, 1061, 1062, 1072, 1081, 1091, 1110, 1111, 1130,
            1131, 1132, 1150, 1151, 1160, 1161, 1170, 1171, 1172, 2000, 2001, 2012, 2022, 2030, 2031, 2032,
            2040, 2041, 3000, 3001, 3002, 3010, 3011, 3020, 3021, 4002, 4012, 4022, 4031, 4040, 4041, 4050,
            4051, 4052, 5000, 5001, 5002, 5010, 5011, 5012, 5020, 5021
        };
        for (uint32_t ws : weaponSets) {
            auto* wue = config->add_weapon_unlock();
            wue->set_weapon_set_id(ws);
            wue->mutable_date()->set_seconds(mapDate);
        }

        // Phases from map rotation
        if (rotation.contains("phases") && rotation["phases"].is_array()) {
            for (const auto& phase : rotation["phases"]) {
                auto* p = config->add_phases();

                std::string gachiRule = phase.value("gachiRule", "cVar");
                auto grit = ruleRevMap.find(gachiRule);
                if (grit != ruleRevMap.end()) p->set_gachi_rule(grit->second);
                else p->set_gachi_rule(grpcimpl::boss_config::v1::TURF_WAR);

                p->set_regular_rule(grpcimpl::boss_config::v1::TURF_WAR); // Regular is always turf war

                if (phase.contains("gachiStages") && phase["gachiStages"].is_array()) {
                    for (const auto& s : phase["gachiStages"]) {
                        p->add_gachi_stages(s.get<uint32_t>());
                    }
                }
                if (phase.contains("regularStages") && phase["regularStages"].is_array()) {
                    for (const auto& s : phase["regularStages"]) {
                        p->add_regular_stages(s.get<uint32_t>());
                    }
                }

                p->set_duration(phase.value("duration", 4));
            }
        }

        auto response = co_await grpcimpl::callAsync<
            grpcimpl::boss_config::v1::BossService::Stub,
            void (grpcimpl::boss_config::v1::BossService::Stub::async::*)(
                grpc::ClientContext*,
                const grpcimpl::boss_config::v1::SetVSSettingRequest*,
                google::protobuf::Empty*,
                std::function<void(grpc::Status)>
            ),
            grpcimpl::boss_config::v1::SetVSSettingRequest,
            google::protobuf::Empty
        >(
            stub,
            &grpcimpl::boss_config::v1::BossService::Stub::async::SetVSSetting,
            request,
            30000
        );

        if (response.second.ok()) {
            success = true;
            break;
        }
        logger->log(Logger::level::WARN, Logger::group::GLOBAL_TASKS,
                    "Boss VS setting update failed at " + util::ipv4WPortToString(addr) + ": " + response.second.error_message());
    }

    co_return success;
}

async::Task<void> GlobalTaskScheduler::performDailyMapRotation() const {
    if (!mgmtDb) co_return;

    auto getCmd = db::Database::craftGetSettingCommand("map_rotation");
    auto result = co_await mgmtDb->runCommand(std::move(getCmd));
    nlohmann::json rotation;
    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        co_return;
    }
    try {
        rotation = nlohmann::json::parse(result.getData<std::string>());
    } catch (...) {
        co_return;
    }

    if (!rotation.contains("phases") || !rotation["phases"].is_array() || rotation["phases"].empty()) {
        co_return;
    }

    auto& phases = rotation["phases"];
    const int PHASE_DURATION_HOURS = 4;
    const int TEN_YEAR_DURATION = 87600;

    // Read the stored last rotation time, fall back to epoch 0 (won't rotate on first run)
    auto lastTimeCmd = db::Database::craftGetSettingCommand("last_rotation_time");
    auto lastTimeResult = co_await mgmtDb->runCommand(std::move(lastTimeCmd));
    auto now = std::chrono::system_clock::now();
    long long lastRotationSec = 0;
    if (lastTimeResult.getStatus() == db::DBResultStatus::SUCCESS && lastTimeResult.hasData()) {
        lastRotationSec = std::stoll(lastTimeResult.getData<std::string>());
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
        now - std::chrono::system_clock::from_time_t(lastRotationSec)).count();

    int phasesToRotate = static_cast<int>(elapsed / PHASE_DURATION_HOURS);
    if (phasesToRotate <= 0) co_return;

    // Never remove more phases than exist (minus the last 10yr tail)
    int maxRemovable = static_cast<int>(phases.size()) - 1;
    if (phasesToRotate > maxRemovable) phasesToRotate = maxRemovable;

    // Extract the old tail (10-year), change its duration to 4h since time has passed
    nlohmann::json oldTail = phases.back();
    oldTail["duration"] = PHASE_DURATION_HOURS;

    // Gather middle phases: skip the first phasesToRotate (expired), keep the rest except old tail
    nlohmann::json middlePhases = nlohmann::json::array();
    for (size_t i = phasesToRotate; i < phases.size() - 1; i++) {
        middlePhases.push_back(phases[i]);
    }
    middlePhases.push_back(oldTail);

    // Generate new random phases to fill the gap (phasesToRotate - 1 new 4h phases + 1 new 10yr tail)
    std::vector<uint32_t> maps = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    std::vector<std::string> rules = {"cVar", "cVlf", "cVgl"};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> mapDist(0, (int)maps.size() - 1);
    std::uniform_int_distribution<> ruleDist(0, (int)rules.size() - 1);

    nlohmann::json freshPhases = nlohmann::json::array();
    for (int i = 0; i < phasesToRotate - 1; i++) {
        std::string gachiRule = rules[ruleDist(gen)];
        uint32_t gachiStage1 = maps[mapDist(gen)];
        uint32_t gachiStage2 = maps[mapDist(gen)];
        while (gachiStage2 == gachiStage1) gachiStage2 = maps[mapDist(gen)];
        uint32_t regularStage1 = maps[mapDist(gen)];
        uint32_t regularStage2 = maps[mapDist(gen)];
        while (regularStage2 == regularStage1) regularStage2 = maps[mapDist(gen)];

        nlohmann::json phase;
        phase["gachiRule"] = gachiRule;
        phase["regularRule"] = "cPnt";
        phase["gachiStages"] = nlohmann::json::array({gachiStage1, gachiStage2});
        phase["regularStages"] = nlohmann::json::array({regularStage1, regularStage2});
        phase["duration"] = PHASE_DURATION_HOURS;
        freshPhases.push_back(phase);
    }

    // Brand new 10-year tail
    {
        std::string gachiRule = rules[ruleDist(gen)];
        uint32_t gachiStage1 = maps[mapDist(gen)];
        uint32_t gachiStage2 = maps[mapDist(gen)];
        while (gachiStage2 == gachiStage1) gachiStage2 = maps[mapDist(gen)];
        uint32_t regularStage1 = maps[mapDist(gen)];
        uint32_t regularStage2 = maps[mapDist(gen)];
        while (regularStage2 == regularStage1) regularStage2 = maps[mapDist(gen)];

        nlohmann::json phase;
        phase["gachiRule"] = gachiRule;
        phase["regularRule"] = "cPnt";
        phase["gachiStages"] = nlohmann::json::array({gachiStage1, gachiStage2});
        phase["regularStages"] = nlohmann::json::array({regularStage1, regularStage2});
        phase["duration"] = TEN_YEAR_DURATION;
        freshPhases.push_back(phase);
    }

    // Assemble: fresh phases + middle + old tail already included in middle
    nlohmann::json finalPhases = nlohmann::json::array();
    for (const auto& p : freshPhases) finalPhases.push_back(p);
    for (const auto& p : middlePhases) finalPhases.push_back(p);

    // Save rotation and updated timestamp
    auto saveCmd = db::Database::craftInsertOrUpdateSettingCommand("map_rotation", rotation.dump());
    co_await mgmtDb->runCommand(std::move(saveCmd));

    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto saveTimeCmd = db::Database::craftInsertOrUpdateSettingCommand("last_rotation_time", std::to_string(nowSec));
    co_await mgmtDb->runCommand(std::move(saveTimeCmd));

    // Queue VS setting update
    if (mgmtDb) {
        auto insertTaskCmd = db::Database::craftInsertTaskCommand(
            static_cast<int>(GlobalTaskType::BOSS_UPDATE_VS_SETTING), "");
        co_await mgmtDb->runCommand(std::move(insertTaskCmd));
    }

    logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS,
                "Map rotation advanced by " + std::to_string(phasesToRotate) + " phases (" +
                std::to_string(elapsed) + " hours elapsed)");
}

} // namespace util