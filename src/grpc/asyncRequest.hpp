#ifndef SPLATOON_SERVER_ASYNCREQUEST_HPP
#define SPLATOON_SERVER_ASYNCREQUEST_HPP

#include <memory>
#include <queue>
#include <utility>

#include <auth.grpc.pb.h>

#include "../util/promise.hpp"

namespace grpcimpl {

/* template <typename Stub, typename Request, typename Response>
class AsyncRequest : public std::enable_shared_from_this<AsyncRequest<Stub, Request, Response>> {
public:
    using ResultType = std::pair<std::shared_ptr<Response>, grpc::Status>;
    using PromisePtr = std::shared_ptr<Promise>;
    using ResultQueue = std::queue<PromisePtr>;
    using AsyncStub = class Stub::async;
    using AsyncMethod = void (AsyncStub::*)(grpc::ClientContext*, const Request*, Response*,
                                       std::function<void(grpc::Status)>);

    AsyncRequest(std::unique_ptr<Stub> stub, AsyncMethod method, std::shared_ptr<ResultQueue> queue,
                std::shared_ptr<std::mutex> queueMutex, std::shared_ptr<std::condition_variable> queueCV) :
                stub(std::move(stub)), method(method), queue(std::move(queue)),
                queueMutex(std::move(queueMutex)),
                queueCV(std::move(queueCV)) {}

    PromisePtr call(std::shared_ptr<Request> request, int timeoutMs = 0) {
        auto promise = std::make_shared<Promise>();
        auto context = std::make_shared<grpc::ClientContext>();

        if (timeoutMs > 0) {
            context->set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeoutMs));
        }

        auto response = std::make_shared<Response>();
        auto self = this->shared_from_this();

        (stub->async()->*method)(context.get(), request.get(), response.get(),
            [self, promise, context = std::move(context), response = std::move(response),
             _req = std::move(request)](grpc::Status status) mutable {
                ResultType result{std::move(response), status};
                promise->setResolveValue(std::move(std::make_any<ResultType>(std::move(result))));

                // Notify the queue that a new result is available
                std::unique_lock lock(*self->queueMutex);
                self->queue->push(promise);
                self->queueCV->notify_one();
            });

        return promise;
    }

private:
    std::unique_ptr<Stub> stub;
    AsyncMethod method;
    std::shared_ptr<ResultQueue> queue;
    std::shared_ptr<std::mutex> queueMutex;
    std::shared_ptr<std::condition_variable> queueCV;
}; */

// Adaptation using corutines and not promises
template <typename Stub, typename Method, typename Request, typename Response>
async::ManualTask<std::pair<std::shared_ptr<Response>, grpc::Status>> callAsync(const std::unique_ptr<Stub>& stub,
                                                                          Method method,
                                                                          std::shared_ptr<Request> request,
                                                                          int timeoutMs = 0) {
    auto context = std::make_shared<grpc::ClientContext>();
    auto response = std::make_shared<Response>();

    if (timeoutMs > 0) {
        context->set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeoutMs));
    }

    auto task = std::make_shared<async::ManualTask<std::pair<std::shared_ptr<Response>, grpc::Status>>>();

    (stub->async()->*method)(context.get(), request.get(), response.get(),
        [task, context = std::move(context), response = std::move(response), _req = std::move(request)]
        (grpc::Status status) mutable {
            // Resolve the promise with the response and status
            task->complete(std::move(std::make_pair(std::move(response), status)));
        });

    return *task;
}

} // namespace grpcimpl

#endif //SPLATOON_SERVER_ASYNCREQUEST_HPP
