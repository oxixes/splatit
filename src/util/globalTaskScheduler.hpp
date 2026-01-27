#ifndef SPLATOON_SERVER_GLOBALTASKSCHEDULER_HPP
#define SPLATOON_SERVER_GLOBALTASKSCHEDULER_HPP

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "../db/database.hpp"
#include "../logger.hpp"
#include "../settingsManager.hpp"
#include "../util/scheduler.hpp"

#define GLOBAL_TASK_PROCESS_INTERVAL_SECONDS 60

namespace util {

enum class GlobalTaskType {
    DELETE_FRIENDS_ACCOUNT = 0
};

// SINGLETON
class GlobalTaskScheduler {
public:
    static void createInstance(std::shared_ptr<db::Database> accountsDb, std::shared_ptr<Logger::Logger> logger,
        std::shared_ptr<SettingsManager> settingsManager); // For initial creation at startup
    static GlobalTaskScheduler& getInstance(); // For getting the instance later

    void stop();

    uint64_t process();

    GlobalTaskScheduler(const GlobalTaskScheduler&) = delete;
    GlobalTaskScheduler& operator=(const GlobalTaskScheduler&) = delete;
private:
    explicit GlobalTaskScheduler(std::shared_ptr<db::Database> accountsDb, std::shared_ptr<Logger::Logger> logger,
                                 std::shared_ptr<SettingsManager> settingsManager)
        : accountsDb(std::move(accountsDb)), logger(std::move(logger)), settingsManager(std::move(settingsManager)),
            schedulerCV(std::make_shared<std::condition_variable>()),
            scheduler(std::make_shared<async::Scheduler>(schedulerCV)) {
        lastProcessTime = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    }

    void schedulerThreadFunc();
    void startScheduler();
    void schedule(async::Task<void>&& task) const;

    [[nodiscard]] async::Task<void> internalProcess() const;
    async::Task<void> deleteTask(const int64_t id) const;

    std::shared_ptr<db::Database> accountsDb;
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<SettingsManager> settingsManager;
    std::shared_ptr<std::condition_variable> schedulerCV;
    std::shared_ptr<async::Scheduler> scheduler;
    std::thread schedulerThread;
    std::mutex schedulerMutex;
    bool shouldStop = false;

    std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> lastProcessTime;
};

} // namespace util

#endif //SPLATOON_SERVER_GLOBALTASKSCHEDULER_HPP