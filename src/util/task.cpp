#include "task.hpp"
#include "scheduler.hpp"

namespace async {

void Task<void>::promise_type::FinalAwaiter::await_suspend(handle_type h) noexcept {
    if (h.promise().continuation && h.promise().scheduler) {
        h.promise().scheduler->schedule_coroutine(h.promise().continuation);
    }
}

void Task<void>::promise_type::complete() {
    if (continuation && scheduler) {
        scheduler->schedule_coroutine(continuation);
    }

    isCompleted = true;
}

void Task<void>::promise_type::setException(const std::exception_ptr& ex) {
    exception = ex;
    if (continuation && scheduler) {
        scheduler->schedule_coroutine(continuation);
    }

    isCompleted = true;
}

void Task<void>::promise_type::setScheduler(std::shared_ptr<Scheduler> s) {
    scheduler = std::move(s);
    if (continuation && scheduler && isCompleted) {
        scheduler->schedule_coroutine(continuation);
    }
}

} // namespace async