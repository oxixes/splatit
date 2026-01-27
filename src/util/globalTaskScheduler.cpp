#include "globalTaskScheduler.hpp"

#include "../db/database.hpp"
#include "../http/account/account.hpp"

namespace util {

std::shared_ptr<GlobalTaskScheduler> globalTaskSchedulerInstance = nullptr;

void GlobalTaskScheduler::createInstance(std::shared_ptr<db::Database> accountsDb, std::shared_ptr<Logger::Logger> logger,
    std::shared_ptr<SettingsManager> settingsManager) {
    if (globalTaskSchedulerInstance != nullptr) {
        throw std::runtime_error("GlobalTaskScheduler instance already created");
    }

    globalTaskSchedulerInstance = std::shared_ptr<GlobalTaskScheduler>(new GlobalTaskScheduler(std::move(accountsDb),
        std::move(logger), std::move(settingsManager)));
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

    if (accountsDb != nullptr) {
        auto getAccTasksCmd = db::Database::craftGetAllTasksCommand();
        const auto getAccTasksResult = co_await accountsDb->runCommand(std::move(getAccTasksCmd));
        if (getAccTasksResult.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::FAILURE, Logger::group::GLOBAL_TASKS, "Failed to retrieve tasks from accounts database");
            co_return;
        }

        const auto tasks = getAccTasksResult.getData<std::vector<db::DBTaskData>>();
        for (const auto& taskData : tasks) {
            logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS, "Processing task ID " + std::to_string(taskData.id) +
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
                    } else {
                        logger->log(Logger::level::INFO, Logger::group::GLOBAL_TASKS,
                                    "Failed to delete friends secure server accounts for PID " + std::to_string(pid));
                    }
                    break;
                }
            }

            processedTasks++;
        }
    }

    logger->log(Logger::level::DEBUG, Logger::group::GLOBAL_TASKS,
                 "Finished processing global tasks. Processed " + std::to_string(processedTasks) + " tasks.");

    co_return;
}

async::Task<void> GlobalTaskScheduler::deleteTask(const int64_t id) const {
    // Delete the task from the database
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

} // namespace util