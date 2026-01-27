#include "internalAccountManagementService.hpp"

#include "../../constants.hpp"

namespace grpcimpl::internalaccountmanagement::v1 {

using namespace async;

Task<void> completeFriendsDeleteServerAccount(grpc::ServerUnaryReactor* reactor,
                                              const DeleteServerAccountRequest* request,
                                              std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsSecure) {
    if (!co_await friendsSecure->deleteAccount(request->pid())) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to delete friends server account"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* InternalAccountManagementServiceImpl::DeleteServerAccount(grpc::CallbackServerContext* context,
                                                                                    const DeleteServerAccountRequest* request,
                                                                                    google::protobuf::Empty* _) {
    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
                "[" + std::string(InternalAccountManagementService::service_full_name()) + "] "
               "DeleteFriendsServerAccount called for PID: " + std::to_string(request->pid())
               + " and game server id: " + request->gameserverid());

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    // Schedule the task to complete the request
    if (request->gameserverid() == FRIENDS_SERVER_ID && friendsSecure != nullptr) {
        auto task = completeFriendsDeleteServerAccount(reactor, request, friendsSecure);
        task.setContext(reactor);
        friendsSecure->scheduleArbitraryFunction(std::move(task));
    } else {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Game server ID not found"));
    }

    return reactor;
}

} // namespace grpcimpl::internalaccountmanagement::v1