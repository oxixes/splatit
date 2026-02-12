#ifndef MANUALTASK_HPP
#define MANUALTASK_HPP

#ifdef __clang__
    #if __clang_major__ >= 15 || (__clang_major__ == 14 && (__clang_minor__ > 0 || (__clang_minor__ == 0 && __clang_patchlevel__ >= 6)))
        #include <coroutine>
    #else
        #include <experimental/coroutine>
        namespace std {
            using namespace experimental;  // Bring std::experimental into the std namespace
        }
#endif
#else
#include <coroutine>
#endif
#include <memory>

#include "task.hpp"
#include "scheduler.hpp"

namespace async {

template <typename T>
class ManualTask {
public:
    explicit ManualTask() : state(std::make_shared<State>()) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        auto& prom = std::coroutine_handle<Task<void>::promise_type>::from_address(awaiting.address()).promise();
        state->scheduler = prom.scheduler;

        state->continuation = awaiting;
        if (state->scheduler && state->result.has_value()) {
            state->scheduler->schedule_coroutine(state->continuation, state);
        }
    }

    T await_resume() noexcept {
        return std::move(*state->result);
    }

    void complete(T&& value) {
        state->result = std::move(value);
        if (state->continuation && state->scheduler) {
            state->scheduler->schedule_coroutine(state->continuation, state);
        }
    }

private:
    struct State {
        std::shared_ptr<Scheduler> scheduler;
        std::coroutine_handle<> continuation;
        std::optional<T> result;
    };

    std::shared_ptr<State> state;
};

template <>
class ManualTask<void> {
public:
    explicit ManualTask() : state(std::make_shared<State>()) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        auto& prom = std::coroutine_handle<Task<void>::promise_type>::from_address(awaiting.address()).promise();
        state->scheduler = prom.scheduler;

        state->continuation = awaiting;
        if (state->scheduler && state->result) {
            state->scheduler->schedule_coroutine(state->continuation, state);
        }
    }

    void await_resume() noexcept {}

    void complete() {
        state->result = true;
        if (state->continuation && state->scheduler) {
            state->scheduler->schedule_coroutine(state->continuation, state);
        }
    }

private:
    struct State {
        std::shared_ptr<Scheduler> scheduler;
        std::coroutine_handle<> continuation;
        bool result = false;
    };

    std::shared_ptr<State> state;
};

template <typename T>
Task<std::vector<T>> waitForAll(std::vector<ManualTask<T>>&& tasks) {
    std::vector<T> results;
    results.reserve(tasks.size());
    for (auto& task : tasks) {
        results.push_back(std::move(co_await task));
    }

    co_return std::move(results);
}

} // namespace async

#endif //MANUALTASK_HPP
