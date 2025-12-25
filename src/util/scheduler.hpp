#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include <mutex>
#include <queue>
#include <condition_variable>

#include "task.hpp"

namespace async {

class Scheduler {
public:
    explicit Scheduler(std::shared_ptr<std::condition_variable> cv) : cv(std::move(cv)) {}
    void schedule(Task<void>&& task);
    void schedule_coroutine(std::coroutine_handle<> h, std::shared_ptr<void> keep_alive = nullptr);
    [[nodiscard]] bool hasTasks();
    std::unique_ptr<Task<void>> getTask();
    static void run(const std::unique_ptr<Task<void>>& task);
    void clear();

private:
    std::queue<Task<void>> queue;
    std::queue<std::pair<std::coroutine_handle<>, std::shared_ptr<void>>> handles;
    std::mutex queueMutex;

    std::shared_ptr<std::condition_variable> cv;
};

} // namespace async

#endif //SCHEDULER_HPP
