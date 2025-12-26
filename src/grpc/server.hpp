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

namespace grpcimpl {

struct ServerPtrs {
    std::shared_ptr<SettingsManager> settingsManager;
    std::shared_ptr<http::Server> httpServer;
    std::shared_ptr<nex::rmc::AuthRMC> friendsAuthRMC;
    std::shared_ptr<nex::rmc::AuthRMC> splatoonAuthRMC;
    std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsSecureRMC;
    std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonSecureRMC;
};

class Server {
public:
    Server(std::shared_ptr<Logger::Logger> logger, sock::IPv4Addr listenDir, bool reflection, ServerPtrs serverPtrs);
    ~Server();

    void listen();
    void stop();

private:
    void serverThread();

    std::shared_ptr<Logger::Logger> logger;
    sock::IPv4Addr listenDir{};
    ServerPtrs serverPtrs;

    std::thread serverThreadHandle;
    std::unique_ptr<grpc::Server> grpcServer;

    std::atomic<bool> running = false;

    std::shared_ptr<grpcimpl::auth::v1::AuthServiceImpl> authService;
    std::shared_ptr<grpcimpl::serverstatus::v1::ServerStatusServiceImpl> serverStatusService;

    bool reflectionEnabled = false;
};

} // namespace grpc

#endif //SPLATOON_SERVER_GRPC_SERVER_HPP
