#ifndef TASKSYNC_HPP
#define TASKSYNC_HPP

#include "task.hpp"
#include "scheduler.hpp"

namespace async {

template<typename T>
T runTaskSync(Task<T> task) {
    // Shared state for synchronization
    struct SharedState {
        std::mutex mtx;
        std::condition_variable cv;
        bool completed = false;
        std::optional<T> result;
        std::exception_ptr exception = nullptr;
    };

    auto state = std::make_shared<SharedState>();

    // Set up a scheduler to run the coroutine
    auto cv = std::make_shared<std::condition_variable>();
    auto scheduler = std::make_shared<Scheduler>(cv);

    // Create a task that wraps the original and stores the result
    Task<void> wrapper = [](Task<T> inner, std::shared_ptr<SharedState> state) -> Task<void> {
        try {
            T result = co_await inner;

            // Store the result and notify
            std::lock_guard lock(state->mtx);
            state->result = std::move(result);
            state->completed = true;
            state->cv.notify_one();
        } catch (...) {
            // Capture any exception
            std::lock_guard lock(state->mtx);
            state->exception = std::current_exception();
            state->completed = true;
            state->cv.notify_one();
        }
        co_return;
    }(std::move(task), state);

    // Program the task with our scheduler
    scheduler->schedule(std::move(wrapper));

    // Run tasks until ours is complete
    while (true) {
        {
            std::unique_lock<std::mutex> lock(state->mtx);
            if (state->completed) break;
        }

        if (scheduler->hasTasks()) {
            auto nextTask = scheduler->getTask();
            if (nextTask) {
                Scheduler::run(nextTask);
            }
        }

        // Brief pause to avoid excessive CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Check for exceptions
    if (state->exception) {
        std::rethrow_exception(state->exception);
    }

    // Return the result
    return std::move(*state->result);
}

// Specialization for Task<void>
inline void runTaskSync(Task<void> task) {
    // Shared state for synchronization
    struct SharedState {
        std::mutex mtx;
        std::condition_variable cv;
        bool completed = false;
        std::exception_ptr exception = nullptr;
    };

    auto state = std::make_shared<SharedState>();

    // Set up a scheduler
    auto cv = std::make_shared<std::condition_variable>();
    auto scheduler = std::make_shared<Scheduler>(cv);

    // Create a task that wraps the original
    Task<void> wrapper = [](Task<void> inner, std::shared_ptr<SharedState> state) -> Task<void> {
        try {
            co_await inner;

            // Notify completion
            std::lock_guard lock(state->mtx);
            state->completed = true;
            state->cv.notify_one();
        } catch (...) {
            // Capture any exception
            std::lock_guard lock(state->mtx);
            state->exception = std::current_exception();
            state->completed = true;
            state->cv.notify_one();
        }
        co_return;
    }(std::move(task), state);

    // Program the task
    scheduler->schedule(std::move(wrapper));

    // Run tasks until ours is complete
    while (true) {
        {
            std::unique_lock lock(state->mtx);
            if (state->completed) break;
        }

        if (scheduler->hasTasks()) {
            auto nextTask = scheduler->getTask();
            if (nextTask) {
                Scheduler::run(nextTask);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Check for exceptions
    if (state->exception) {
        std::rethrow_exception(state->exception);
    }
}

} // namespace async

#endif //TASKSYNC_HPP
