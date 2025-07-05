#include "example.hpp"

namespace grpcimpl::example {

grpc::ServerUnaryReactor* GreeterServiceImpl::SayHello(grpc::CallbackServerContext* context,
                                                       const grpcimpl::example::HelloRequest* request,
                                                       HelloReply* reply) {
    std::string prefix("Caca culo pedo pis: ");
    reply->set_message(prefix + request->name());

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
}

} // namespace grpcimpl::example