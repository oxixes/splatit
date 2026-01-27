#ifndef SPLATOON_SERVER_AUTHSERVICE_HPP
#define SPLATOON_SERVER_AUTHSERVICE_HPP

#include <auth.grpc.pb.h>

#include "../../nex/auth/auth.hpp"

namespace grpcimpl::auth::v1 {

class AuthServiceImpl final : public AuthService::CallbackService {
public:
    explicit AuthServiceImpl(std::shared_ptr<nex::rmc::AuthRMC> friendsAuth,
                             std::shared_ptr<nex::rmc::AuthRMC> splatoonAuth,
                             std::shared_ptr<Logger::Logger> logger) :
                             logger(std::move(logger)),
                             friendsAuth(std::move(friendsAuth)),
                             splatoonAuth(std::move(splatoonAuth)) {}

    grpc::ServerUnaryReactor* GetGameServerCredentials(grpc::CallbackServerContext* context,
                                       const GetGameServerCredentialsRequest* request,
                                       GetGameServerCredentialsResponse* reply) override;

    grpc::ServerUnaryReactor* DeleteGameServerCredentials(grpc::CallbackServerContext* context,
                                       const DeleteGameServerCredentialsRequest* request,
                                       google::protobuf::Empty* _) override;

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<nex::rmc::AuthRMC> friendsAuth;
    std::shared_ptr<nex::rmc::AuthRMC> splatoonAuth;
};

} // namespace grpcimpl::auth::v1

#endif //SPLATOON_SERVER_AUTHSERVICE_HPP
