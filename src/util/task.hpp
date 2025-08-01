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
        std::optional<T> value;
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

        void return_value(T v) { value = std::move(v); }

        void unhandled_exception() { exception = std::current_exception(); }

        void complete(T&& v);
        void setException(const std::exception_ptr& ex);

        void setScheduler(std::shared_ptr<Scheduler> s);
    };

    explicit Task(handle_type h) : coro(h)
    {
        std::cout << "Creating task " << this << " with coro " << coro.address() << std::endl;
    }
    Task(Task&& other) noexcept : coro(other.coro)
    {
        other.coro = nullptr;
        std::cout << "Moving task " << this << " with coro " << coro.address() << std::endl;
    }
    ~Task() {
        std::cout << "Destroying task " << this << " with coro " << coro.address() << std::endl;
        if (coro) coro.destroy();
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            std::cout << "A Destroying coro " << coro.address() << std::endl;
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

        void await_suspend(std::coroutine_handle<> awaiting) {
            coro.promise().continuation = awaiting;
        }


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
    void setException(const std::exception_ptr& ex) const { coro.promise().setException(ex); }
    void setScheduler(std::shared_ptr<Scheduler> s) const { coro.promise().setScheduler(std::move(s)); }
    void complete(T&& v) const
    {
        coro.promise().complete(std::move(v));
        std::cout << "Completing task " << this << " with coro " << coro.address() << std::endl;
    }
    std::optional<T> result() const { return coro.promise().value; }

    // Factory method to create a Task manually (instead of using co_await)
    static std::pair<Task, promise_type*> createManual() {
        auto* p = new promise_type();
        auto h = handle_type::from_promise(*p);
        auto sp = std::shared_ptr<promise_type>(p, [](promise_type* ptr) {
            std::cout << "A Destroying coro " << ptr << std::endl;
            // handle_type::from_promise(*ptr).destroy();
        });
        std::cout << "Creating manual task " << h.address() << std::endl;
        return { Task{h}, p };
    }

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

        void complete();
        void setException(const std::exception_ptr& ex);

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

        void await_suspend(std::coroutine_handle<> awaiting) {
            coro.promise().continuation = awaiting;
        }

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
    void setException(const std::exception_ptr& ex) const { coro.promise().setException(ex); }
    void setScheduler(std::shared_ptr<Scheduler> s) const { coro.promise().setScheduler(std::move(s)); }
    void complete() const { coro.promise().complete(); }

    static std::pair<Task, std::shared_ptr<promise_type>> createManual() {
        auto* p = new promise_type();
        auto h = handle_type::from_promise(*p);
        auto sp = std::shared_ptr<promise_type>(p, [](promise_type* ptr) {
            handle_type::from_promise(*ptr).destroy();
        });
        return { Task{h}, sp };
    }

    handle_type coro;
};



template <typename T>
Task<T> spawn(std::shared_ptr<Scheduler> scheduler, Task<T>&& task) {
    task.setScheduler(std::move(scheduler));
    return std::move(task);
}

template <typename T>
Task<std::vector<T>> waitForAll(std::vector<Task<T>>&& tasks) {
    std::vector<T> results;
    results.reserve(tasks.size());
    for (auto& task : tasks) {
        results.push_back(std::move(co_await task));
    }

    co_return std::move(results);
}

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
void Task<T>::promise_type::complete(T&& v) {
    value = std::move(v);
    if (continuation && scheduler) {
        scheduler->schedule_coroutine(continuation);
    }

    isCompleted = true;
}

template <typename T>
void Task<T>::promise_type::setException(const std::exception_ptr& ex) {
    exception = ex;
    if (continuation && scheduler) {
        scheduler->schedule_coroutine(continuation);
    }

    isCompleted = true;
}

template <typename T>
void Task<T>::promise_type::setScheduler(std::shared_ptr<Scheduler> s) {
    scheduler = std::move(s);
    if (continuation && scheduler && isCompleted) {
        scheduler->schedule_coroutine(continuation);
    }
}

} // namespace async

#endif //TASK_HPP
