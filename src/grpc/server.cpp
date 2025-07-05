#include "server.hpp"
#include "../util/util.hpp"

#include <utility>
#include <thread>

#include <grpcpp/ext/proto_server_reflection_plugin.h>

namespace grpcimpl {

Server::Server(std::shared_ptr<Logger::Logger> logger, sock::IPv4Addr listenDir, bool reflection) {
    this->logger = std::move(logger);
    this->listenDir = listenDir;
    this->reflectionEnabled = reflection;
}

Server::~Server() {
    if (running) stop();
}

void Server::listen() {
    if (running) return;

    std::string listenIPv4 = util::ipv4ToString(listenDir);

    greeterService = std::make_shared<grpcimpl::example::GreeterServiceImpl>();

    if (reflectionEnabled) {
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    }
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listenIPv4 + ":" + std::to_string(listenDir.port), grpc::InsecureServerCredentials());
    builder.RegisterService(greeterService.get());
    grpcServer = builder.BuildAndStart();
    if (!grpcServer) {
        throw std::runtime_error("Failed to start gRPC server on " + listenIPv4 + ":" + std::to_string(listenDir.port));
    }

    logger->log(Logger::level::INFO, Logger::group::GRPC, "gRPC server listening on " + listenIPv4 + ":" + std::to_string(listenDir.port));

    running = true;
    serverThreadHandle = std::thread(&Server::serverThread, this);
}

void Server::stop() {
    if (!running) return;

    grpcServer->Shutdown();

    if (serverThreadHandle.joinable()) {
        serverThreadHandle.join();
    }

    running = false;
    logger->log(Logger::level::INFO, Logger::group::GRPC, "gRPC server stopped");
}

void Server::serverThread() {
    if (grpcServer) {
        grpcServer->Wait();
    } else {
        logger->log(Logger::level::FAILURE, Logger::group::GRPC, "gRPC server is not initialized");
    }
}

} // namespace grpcimpl