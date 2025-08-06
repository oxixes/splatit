#ifndef TASK_HPP
#define TASK_HPP

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
#include <optional>
#include <exception>
#include <any>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <iostream>

namespace async {

class Scheduler; // Forward declaration for Scheduler

template <typename T>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::exception_ptr exception;
        std::any context;
        std::coroutine_handle<> continuation = nullptr;
        std::shared_ptr<Scheduler> scheduler = nullptr;
        std::atomic<bool> isCompleted{false};
        std::optional<T> value;

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(handle_type h) noexcept;
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_value(T v) { value = std::move(v); }

        void unhandled_exception() { exception = std::current_exception(); }

        void setScheduler(std::shared_ptr<Scheduler> s);
    };

    explicit Task(handle_type h) : coro(h) {}
    Task(Task&& other) noexcept : coro(other.coro)
    {
        other.coro = nullptr;
    }
    ~Task() {
        if (coro) coro.destroy();
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro) coro.destroy();
            coro = other.coro;
            other.coro = nullptr;
        }
        return *this;
    }

    struct Awaiter {
        handle_type coro;
        explicit Awaiter(handle_type h) : coro(h) {}
        bool await_ready() { return coro.done(); }

        void await_suspend(std::coroutine_handle<> awaiting);

        T await_resume() {
            if (coro.promise().exception)
                std::rethrow_exception(coro.promise().exception);
            return std::move(*coro.promise().value);
        }
    };

    auto operator co_await() { return Awaiter{coro}; }

    void setContext(std::any&& v) {
        coro.promise().context = std::move(v);
    }

    [[nodiscard]] std::any& getContext() {
        return coro.promise().context;
    }

    [[nodiscard]] bool done() const { return coro.done(); }
    [[nodiscard]] bool hasException() const { return coro.promise().exception != nullptr; }
    [[nodiscard]] std::exception_ptr getException() const { return coro.promise().exception; }
    void setScheduler(std::shared_ptr<Scheduler> s) const { coro.promise().setScheduler(std::move(s)); }
    std::optional<T> result() const { return coro.promise().value; }

    handle_type coro;
};

// Specialization for void return type
// This allows us to use Task<void> for coroutines that do not return a value.
template <>
class Task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::exception_ptr exception;
        std::any context;
        std::coroutine_handle<> continuation = nullptr;
        std::shared_ptr<Scheduler> scheduler = nullptr;
        std::atomic<bool> isCompleted{false};

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(handle_type h) noexcept;
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_void() {}

        void unhandled_exception() { exception = std::current_exception(); }

        void setScheduler(std::shared_ptr<Scheduler> s);
    };

    explicit Task(handle_type h) : coro(h) {}
    Task(Task&& other) noexcept : coro(other.coro) { other.coro = nullptr; }
    ~Task() {
        if (coro) coro.destroy();
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro) coro.destroy();
            coro = other.coro;
            other.coro = nullptr;
        }
        return *this;
    }

    struct Awaiter {
        handle_type coro;
        explicit Awaiter(handle_type h) : coro(h) {}
        bool await_ready() { return coro.done(); }

        void await_suspend(std::coroutine_handle<> awaiting);

        void await_resume() {
            if (coro.promise().exception)
                std::rethrow_exception(coro.promise().exception);
        }
    };

    auto operator co_await() const { return Awaiter{coro}; }

    void setContext(std::any&& v) const {
        coro.promise().context = std::move(v);
    }

    [[nodiscard]] std::any& getContext() const {
        return coro.promise().context;
    }

    [[nodiscard]] bool done() const { return coro.done(); }
    [[nodiscard]] bool hasException() const { return coro.promise().exception != nullptr; }
    [[nodiscard]] std::exception_ptr getException() const { return coro.promise().exception; }
    void setScheduler(std::shared_ptr<Scheduler> s) const { coro.promise().setScheduler(std::move(s)); }

    handle_type coro;
};

/* template <typename T>
Task<T> spawn(std::shared_ptr<Scheduler> scheduler, Task<T>&& task) {
    task.setScheduler(std::move(scheduler));
    return std::move(task);
} */

} // namespace async

#include "scheduler.hpp"

namespace async {

template <typename T>
void Task<T>::promise_type::FinalAwaiter::await_suspend(handle_type h) noexcept {
    if (h.promise().continuation && h.promise().scheduler) {
        h.promise().scheduler->schedule_coroutine(h.promise().continuation);
    }
}

template <typename T>
void Task<T>::promise_type::setScheduler(std::shared_ptr<Scheduler> s) {
    scheduler = std::move(s);
    if (continuation && scheduler) {
        scheduler->schedule_coroutine(continuation);
    }
}

template <typename T>
void Task<T>::Awaiter::await_suspend(std::coroutine_handle<> awaiting) {
    // This assumes the type of the awaiting coroutine is Task<T>. It may not be, but we only get the scheduler
    // which should have the same offset in the promise_type. In case of modification, one should ensure that this
    // remains valid.
    auto& prom = std::coroutine_handle<promise_type>::from_address(awaiting.address()).promise();
    coro.promise().setScheduler(prom.scheduler);
    coro.promise().continuation = awaiting;
    coro.promise().scheduler->schedule_coroutine(coro);
}

} // namespace async

#endif //TASK_HPP
