#include "task.hpp"
#include "scheduler.hpp"

namespace async {

void Scheduler::schedule(Task<void>&& task) {
    std::lock_guard lock(queueMutex);
    queue.push(std::move(task));
    cv->notify_one();
}

void Scheduler::schedule_coroutine(std::coroutine_handle<> h, std::shared_ptr<void> keep_alive) {
    std::lock_guard lock(queueMutex);
    handles.emplace(h, keep_alive);
    cv->notify_one();
}

bool Scheduler::hasTasks() {
    std::unique_lock lock(queueMutex);
    return !queue.empty() || !handles.empty();
}

std::unique_ptr<Task<void>> Scheduler::getTask() {
    std::unique_lock lock(queueMutex);
    if (queue.empty() && handles.empty()) return nullptr;

    Task<void> task{nullptr};

    if (!handles.empty()) {
        std::pair<std::coroutine_handle<>, std::shared_ptr<void>> handle = handles.front();
        handles.pop();

        // Convert the coroutine handle to a Task<void>
        task = Task<void>{Task<void>::handle_type::from_address(handle.first.address())};
        task.setContext(handle.second);
    } else if (!queue.empty()) {
        task = std::move(queue.front());
        queue.pop();
    }

    return std::make_unique<Task<void>>(std::move(task));
}

void Scheduler::run(const std::unique_ptr<Task<void>>& task) {
    task->coro.resume();

    if (task->done() && task->hasException()) {
        std::rethrow_exception(task->getException());
    }
}

void Scheduler::clear() {
    std::lock_guard lock(queueMutex);
    while (!queue.empty()) {
        queue.pop();
    }
    while (!handles.empty()) {
        handles.pop();
    }
    cv->notify_all();
}


} // namespace async