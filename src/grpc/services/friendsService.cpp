#include "friendsService.hpp"

namespace grpcimpl::friends::v1 {

async::Task<void> completeSendNotification(grpc::ServerUnaryReactor* reactor, const SendNotificationRequest* request,
                                        const std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsRMC) {
    sock::IPv4Addr ipv4Addr {
        .a = static_cast<uint8_t>(request->clientinfo().address().address().a()),
        .b = static_cast<uint8_t>(request->clientinfo().address().address().b()),
        .c = static_cast<uint8_t>(request->clientinfo().address().address().c()),
        .d = static_cast<uint8_t>(request->clientinfo().address().address().d()),
        .port = static_cast<uint16_t>(request->clientinfo().address().address().port())
    };

    const nex::prudp::PRUDPAddress prudpAddress {
        .address = ipv4Addr,
        .vPort = static_cast<uint8_t>(request->clientinfo().address().vport()),
        .streamType = static_cast<uint8_t>(request->clientinfo().address().streamtype()),
        .srcVPort = static_cast<uint8_t>(request->clientinfo().address().srcvport()),
        .srcStreamType = static_cast<uint8_t>(request->clientinfo().address().srcstreamtype())
    };

    const nex::rmc::ClientInfo clientInfo {
        .address = prudpAddress,
        .minorVersion = static_cast<uint8_t>(request->clientinfo().minorversion()),
        .substreamId = static_cast<uint8_t>(request->clientinfo().substreamid()),
        .serverId = request->clientinfo().serverid(),
        .pid = request->clientinfo().pid()
    };

    const auto type = static_cast<nex::rmc::NintendoNotificationType>(request->type());

    nex::rmc::AnyDataHolder data(clientInfo.minorVersion);
    const std::vector<uint8_t> rawData(request->data().data().begin(), request->data().data().end());
    data.setRaw(rawData, request->data().type());

    if (!co_await friendsRMC->sendNotification(clientInfo, type, request->sender(), data, true)) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to send notification"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* FriendsServiceImpl::SendNotification(grpc::CallbackServerContext* context,
    const SendNotificationRequest* request, google::protobuf::Empty *_) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
                "[" + std::string(FriendsService::service_full_name()) + "] "
                "SendNotification called for PID: " + std::to_string(request->clientinfo().pid())
                + ", NotificationType: " + std::to_string(request->type()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeSendNotification(reactor, request, friendsRMC);
    task.setContext(reactor);
    friendsRMC->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

} // namespace grpcimpl::friends::v1