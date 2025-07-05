#ifndef SPLATOON_SERVER_EXAMPLE_HPP
#define SPLATOON_SERVER_EXAMPLE_HPP

#include <example.grpc.pb.h>

namespace grpcimpl::example {

class GreeterServiceImpl final : public grpcimpl::example::Greeter::CallbackService {
public:
    GreeterServiceImpl() = default;

    grpc::ServerUnaryReactor* SayHello(grpc::CallbackServerContext* context,
                                       const grpcimpl::example::HelloRequest* request,
                                       HelloReply* reply) override;
};

} // namespace grpcimpl::example

#endif //SPLATOON_SERVER_EXAMPLE_HPP
