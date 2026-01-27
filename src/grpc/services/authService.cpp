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
                "[" + std::string(AuthService::service_full_name()) + "] "
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
        auto task = completeGetGameServerCredentials(reactor, reply, request, authRMC);
        task.setContext(reactor);
        authRMC->scheduleArbitraryFunction(std::move(task));
    }

    return reactor;
}

Task<void> completeDeleteGameServerCredentials(grpc::ServerUnaryReactor* reactor,
                                               const DeleteGameServerCredentialsRequest* request,
                                               std::shared_ptr<nex::rmc::AuthRMC> authRMC) {
    if (!co_await authRMC->deleteGameServerAccess(request->pid())) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to delete game server credentials"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AuthServiceImpl::DeleteGameServerCredentials(grpc::CallbackServerContext* context,
                                                                       const DeleteGameServerCredentialsRequest* request,
                                                                       google::protobuf::Empty* _) {
    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
                "[" + std::string(AuthService::service_full_name()) + "] "
               "DeleteGameServerCredentials called for GameServerID: " + request->gameserverid()
               + ", PID: " + std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    std::shared_ptr<nex::rmc::AuthRMC> authRMC = nullptr;

    if (request->gameserverid() == FRIENDS_SERVER_ID) {
        authRMC = friendsAuth;
    } else if (request->gameserverid() == SPLATOON_SERVER_ID) {
        authRMC = splatoonAuth;
    }

    if (!authRMC) {
        reactor->Finish(grpc::Status::OK);
    } else {
        // Schedule the task to complete the request
        auto task = completeDeleteGameServerCredentials(reactor, request, authRMC);
        task.setContext(reactor);
        authRMC->scheduleArbitraryFunction(std::move(task));
    }

    return reactor;
}

} // namespace grpcimpl::auth::v1