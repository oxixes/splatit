#ifndef SPLATOON_SERVER_GRPC_SERVER_HPP
#define SPLATOON_SERVER_GRPC_SERVER_HPP

#include <memory>
#include <thread>
#include <grpcpp/grpcpp.h>

#include "../logger.hpp"
#include "../socket/socket.hpp"
#include "../http/server.hpp"
#include "../nex/auth/auth.hpp"
#include "../nex/friends/friendsSecure.hpp"
#include "../nex/splatoon/splatoonSecure.hpp"
#include "services/authService.hpp"
#include "services/serverStatusService.hpp"
#include "services/accountManagementService.hpp"
#include "services/friendsService.hpp"
#include "services/internalAccountManagementService.hpp"

namespace grpcimpl {

struct gRPCServerData {
    std::shared_ptr<SettingsManager> settingsManager;
    std::shared_ptr<crypto::CertManager> certManager;
    std::shared_ptr<db::Database> accountDatabase;

    std::shared_ptr<http::Server> httpServer;
    std::shared_ptr<nex::rmc::AuthRMC> friendsAuthRMC;
    std::shared_ptr<nex::rmc::AuthRMC> splatoonAuthRMC;
    std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsSecureRMC;
    std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonSecureRMC;
};

class Server {
public:
    Server(std::shared_ptr<Logger::Logger> logger, sock::IPv4Addr listenDir, bool reflection, gRPCServerData serverData);
    ~Server();

    void listen();
    void stop();

private:
    void serverThread();

    std::shared_ptr<Logger::Logger> logger;
    sock::IPv4Addr listenDir{};
    gRPCServerData serverData;

    std::thread serverThreadHandle;
    std::unique_ptr<grpc::Server> grpcServer;

    std::atomic<bool> running = false;

    std::shared_ptr<grpcimpl::auth::v1::AuthServiceImpl> authService;
    std::shared_ptr<grpcimpl::internalaccountmanagement::v1::InternalAccountManagementServiceImpl> internalAccountManagementService;
    std::shared_ptr<grpcimpl::serverstatus::v1::ServerStatusServiceImpl> serverStatusService;
    std::shared_ptr<grpcimpl::accountmanagement::v1::AccountManagementServiceImpl> accountManagementService;
    std::shared_ptr<grpcimpl::friends::v1::FriendsServiceImpl> friendsService;

    bool reflectionEnabled = false;
};

} // namespace grpc

#endif //SPLATOON_SERVER_GRPC_SERVER_HPP
