#ifndef SPLATOON_SERVER_GRPC_SERVER_HPP
#define SPLATOON_SERVER_GRPC_SERVER_HPP

#include <memory>
#include <thread>
#include <grpcpp/grpcpp.h>

#include "../logger.hpp"
#include "../socket/socket.hpp"
#include "services/example.hpp"

namespace grpcimpl {

class Server {
public:
    Server(std::shared_ptr<Logger::Logger> logger, sock::IPv4Addr listenDir);
    ~Server();

    void listen();
    void stop();

private:
    void serverThread();

    std::shared_ptr<Logger::Logger> logger;
    sock::IPv4Addr listenDir{};

    std::thread serverThreadHandle;
    std::unique_ptr<grpc::Server> grpcServer;

    std::atomic<bool> running = false;

    std::shared_ptr<grpcimpl::example::GreeterServiceImpl> greeterService;
};

} // namespace grpc

#endif //SPLATOON_SERVER_GRPC_SERVER_HPP
