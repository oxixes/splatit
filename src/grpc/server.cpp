#include "server.hpp"

#include <utility>
#include <thread>
#include <fstream>
#include <sstream>

#include <grpcpp/ext/proto_server_reflection_plugin.h>

namespace grpcimpl {

static std::string readFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

Server::Server(std::shared_ptr<Logger::Logger> logger, sock::IPv4Addr listenDir, bool reflection, gRPCServerData serverData,
               bool tlsEnabled, fs::path tlsCertPath, fs::path tlsKeyPath, fs::path tlsCaCertPath) {
    this->logger = std::move(logger);
    this->listenDir = listenDir;
    this->reflectionEnabled = reflection;
    this->serverData = std::move(serverData);
    this->tlsEnabled = tlsEnabled;
    this->tlsCertPath = std::move(tlsCertPath);
    this->tlsKeyPath = std::move(tlsKeyPath);
    this->tlsCaCertPath = std::move(tlsCaCertPath);
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
        friendsService = std::make_shared<grpcimpl::friends::v1::FriendsServiceImpl>(
                logger, serverData.friendsSecureRMC);
    }

    if (serverData.splatoonSecureRMC != nullptr) {
        splatoonService = std::make_shared<grpcimpl::splatoon::v1::SplatoonServiceImpl>(
                logger, serverData.splatoonSecureRMC);
    }

    if (serverData.friendsSecureRMC != nullptr) {
        internalAccountManagementService = std::make_shared<grpcimpl::internalaccountmanagement::v1::InternalAccountManagementServiceImpl>(
                serverData.friendsSecureRMC, logger);
    }

    if (serverData.settingsManager->isAccountEnabled()) {
        accountManagementService = std::make_shared<grpcimpl::accountmanagement::v1::AccountManagementServiceImpl>(
                logger, serverData.accountDatabase, serverData.httpServer, serverData.settingsManager, serverData.certManager);
    }

    if (serverData.bossDatabase != nullptr) {
        bossService = std::make_shared<grpcimpl::boss_config::v1::BossServiceImpl>(
                logger, serverData.settingsManager, serverData.bossDatabase, serverData.httpServer);
    }

    serverStatusService = std::make_shared<grpcimpl::serverstatus::v1::ServerStatusServiceImpl>(
            logger, serverData.settingsManager);

    if (reflectionEnabled) {
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    }
    grpc::ServerBuilder builder;

    std::shared_ptr<grpc::ServerCredentials> creds;
    if (tlsEnabled) {
        grpc::SslServerCredentialsOptions tlsOptions;
        tlsOptions.pem_root_certs = readFile(tlsCaCertPath);
        tlsOptions.pem_key_cert_pairs.push_back({
            readFile(tlsKeyPath),
            readFile(tlsCertPath)
        });
        tlsOptions.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
        creds = grpc::SslServerCredentials(tlsOptions);
        logger->log(Logger::level::INFO, Logger::group::GRPC,
                    "gRPC server using TLS with client certificate verification");
    } else {
        creds = grpc::InsecureServerCredentials();
    }

    builder.AddListeningPort(listenIPv4 + ":" + std::to_string(listenDir.port), creds);

    if (authService != nullptr) builder.RegisterService(authService.get());
    if (internalAccountManagementService != nullptr) builder.RegisterService(internalAccountManagementService.get());
    builder.RegisterService(serverStatusService.get());
    if (accountManagementService != nullptr) builder.RegisterService(accountManagementService.get());
    if (friendsService != nullptr) builder.RegisterService(friendsService.get());
    if (splatoonService != nullptr) builder.RegisterService(splatoonService.get());
    if (bossService != nullptr) builder.RegisterService(bossService.get());

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
