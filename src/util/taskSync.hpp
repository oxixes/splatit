#ifndef TASKSYNC_HPP
#define TASKSYNC_HPP

#include <thread>

#include "task.hpp"
#include "scheduler.hpp"
#include "manualTask.hpp"

namespace async {

template<typename T>
T runManualTaskSync(ManualTask<T> task) {
    // Shared state for synchronization
    struct SharedState {
        std::mutex mtx;
        std::shared_ptr<std::condition_variable> cv;
        bool completed = false;
        std::optional<T> result;
        std::exception_ptr exception = nullptr;
    };

    auto state = std::make_shared<SharedState>();
    state->cv = std::make_shared<std::condition_variable>();

    auto scheduler = std::make_shared<Scheduler>(state->cv);

    // Create a task that wraps the original and stores the result
    Task<void> wrapper = [](ManualTask<T> inner, std::shared_ptr<SharedState> state) -> Task<void> {
        try {
            T result = co_await inner;

            // Store the result and notify
            std::lock_guard lock(state->mtx);
            state->result = std::move(result);
            state->completed = true;
        } catch (...) {
            // Capture any exception
            std::lock_guard lock(state->mtx);
            state->exception = std::current_exception();
            state->completed = true;
        }

        co_return;
    }(std::move(task), state);

    wrapper.setScheduler(scheduler);

    // Program the task with our scheduler
    scheduler->schedule(std::move(wrapper));

    // Run tasks until ours is complete
    while (true) {
        std::unique_lock lock(state->mtx);
        state->cv->wait(lock, [&] { return state->completed || scheduler->hasTasks(); });
        if (state->completed) break;

        lock.unlock();
        scheduler->run(scheduler->getTask());
    }

    // Check for exceptions
    if (state->exception) {
        std::rethrow_exception(state->exception);
    }

    // Return the result
    return std::move(*state->result);
}

} // namespace async

#endif //TASKSYNC_HPP
