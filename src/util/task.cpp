#include "task.hpp"
#include "scheduler.hpp"

namespace async {

void Task<void>::promise_type::FinalAwaiter::await_suspend(handle_type h) noexcept {
    if (h.promise().continuation && h.promise().scheduler) {
        h.promise().scheduler->schedule_coroutine(h.promise().continuation);
    }
}

void Task<void>::promise_type::setScheduler(std::shared_ptr<Scheduler> s) {
    scheduler = std::move(s);
    if (continuation && scheduler) {
        scheduler->schedule_coroutine(continuation);
    }
}

void Task<void>::Awaiter::await_suspend(std::coroutine_handle<> awaiting) {
    // This assumes the type of the awaiting coroutine is Task<void>. It may not be, but we only get the scheduler
    // which should have the same offset in the promise_type. In case of modification, one should ensure that this
    // remains valid.
    auto& prom = std::coroutine_handle<promise_type>::from_address(awaiting.address()).promise();
    coro.promise().setScheduler(prom.scheduler);
    coro.promise().continuation = awaiting;
    coro.promise().scheduler->schedule_coroutine(coro);
}


} // namespace async