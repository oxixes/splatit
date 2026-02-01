#include "server.hpp"

#include <utility>
#include <thread>

#include <grpcpp/ext/proto_server_reflection_plugin.h>

namespace grpcimpl {

Server::Server(std::shared_ptr<Logger::Logger> logger, sock::IPv4Addr listenDir, bool reflection, gRPCServerData serverData) {
    this->logger = std::move(logger);
    this->listenDir = listenDir;
    this->reflectionEnabled = reflection;
    this->serverData = std::move(serverData);
}

Server::~Server() {
    if (running) stop();
}

void Server::listen() {
    if (running) return;

    std::string listenIPv4 = util::ipv4ToString(listenDir);

    if (serverData.friendsAuthRMC != nullptr || serverData.splatoonAuthRMC != nullptr) {
        authService = std::make_shared<grpcimpl::auth::v1::AuthServiceImpl>(
                serverData.friendsAuthRMC, serverData.splatoonAuthRMC, logger);
    }

    if (serverData.friendsSecureRMC != nullptr) {
        internalAccountManagementService = std::make_shared<grpcimpl::internalaccountmanagement::v1::InternalAccountManagementServiceImpl>(
                serverData.friendsSecureRMC, logger);
    }

    if (serverData.settingsManager->isAccountEnabled()) {
        accountManagementService = std::make_shared<grpcimpl::accountmanagement::v1::AccountManagementServiceImpl>(
                logger, serverData.accountDatabase, serverData.httpServer, serverData.settingsManager, serverData.certManager);
    }

    serverStatusService = std::make_shared<grpcimpl::serverstatus::v1::ServerStatusServiceImpl>(
            logger, serverData.settingsManager);

    if (reflectionEnabled) {
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    }
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listenIPv4 + ":" + std::to_string(listenDir.port), grpc::InsecureServerCredentials());

    if (authService != nullptr) builder.RegisterService(authService.get());
    if (internalAccountManagementService != nullptr) builder.RegisterService(internalAccountManagementService.get());
    builder.RegisterService(serverStatusService.get());
    if (accountManagementService != nullptr) builder.RegisterService(accountManagementService.get());

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