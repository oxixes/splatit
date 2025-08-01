#include "authService.hpp"

#include "../../constants.hpp"

namespace grpcimpl::auth::v1 {

using namespace async;

Task<void> completeGetGameServerCredentials(grpc::ServerUnaryReactor* reactor, GetGameServerCredentialsResponse* reply,
                                            const GetGameServerCredentialsRequest* request, std::shared_ptr<nex::rmc::AuthRMC> authRMC) {
    std::optional<std::string> password = co_await authRMC->getOrRegisterUserPassword(request->pid());
    if (password.has_value()) {
        reply->set_success(true);
        reply->set_password(*password);
    } else {
        reply->set_success(false);
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AuthServiceImpl::GetGameServerCredentials(grpc::CallbackServerContext* context,
                                                                    const GetGameServerCredentialsRequest* request,
                                                                    GetGameServerCredentialsResponse* reply) {
    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "GetGameServerCredentials called for GameServerID: " + request->gameserverid()
               + ", PID: " + std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    std::shared_ptr<nex::rmc::AuthRMC> authRMC = nullptr;

    if (request->gameserverid() == FRIENDS_SERVER_ID) {
        authRMC = friendsAuth;
    } else if (request->gameserverid() == SPLATOON_SERVER_ID) {
        authRMC = splatoonAuth;
    }

    if (!authRMC) {
        reply->set_success(false);
        reactor->Finish(grpc::Status::OK);
    } else {
        // Schedule the task to complete the request
        authRMC->scheduleArbitraryFunction(completeGetGameServerCredentials(reactor, reply, request, authRMC));
    }

    return reactor;
}

} // namespace grpcimpl::auth::v1