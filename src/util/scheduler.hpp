#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include <mutex>
#include <queue>

#include "task.hpp"

namespace async {

class Scheduler {
public:
    explicit Scheduler(std::shared_ptr<std::condition_variable> cv) : cv(std::move(cv)) {}
    void schedule(Task<void>&& task);
    void schedule_coroutine(std::coroutine_handle<> h);
    [[nodiscard]] bool hasTasks();
    std::unique_ptr<Task<void>> getTask();
    static void run(const std::unique_ptr<Task<void>>& task);
    void clear();

private:
    std::queue<Task<void>> queue;
    std::queue<std::coroutine_handle<>> handles;
    std::mutex queueMutex;

    std::shared_ptr<std::condition_variable> cv;
};

} // namespace async

#endif //SCHEDULER_HPP
