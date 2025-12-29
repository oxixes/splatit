#include "serverStatusService.hpp"

namespace grpcimpl::serverstatus::v1 {

grpc::ServerUnaryReactor* ServerStatusServiceImpl::GetServerStatus(grpc::CallbackServerContext* context,
                                                                   const GetServerStatusRequest* request,
                                                                   GetServerStatusResponse* reply) {
    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(ServerStatusService::service_full_name()) + "] "
               "GetServerStatus called for Server Type: " + std::to_string(request->servertype()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    switch (request->servertype()) {
        case ACCOUNT:
            reply->set_isonline(settingsMgr->isAccountEnabled());
            break;
        case BOSS:
            reply->set_isonline(settingsMgr->isBOSSEnabled());
            break;
        case FRIENDS_AUTH:
            reply->set_isonline(settingsMgr->isFriendsAuthEnabled());
            break;
        case SPLATOON_AUTH:
            reply->set_isonline(settingsMgr->isSplatoonAuthEnabled());
            break;
        case FRIENDS_SECURE:
            reply->set_isonline(settingsMgr->isFriendsSecureEnabled());
            break;
        case SPLATOON_SECURE:
            reply->set_isonline(settingsMgr->isSplatoonSecureEnabled());
            break;
        default:
            reply->set_isonline(false);
            reply->set_message("Unknown server type");
            reactor->Finish(grpc::Status::OK);
            return reactor;
    }

    if (!reply->isonline()) {
        reply->set_message("Server is disabled");
    }

    reactor->Finish(grpc::Status::OK);

    return reactor;
}

} // namespace grpcimpl::serverstatus::v1