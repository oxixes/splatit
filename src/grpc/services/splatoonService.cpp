#include "splatoonService.hpp"

#include "common.hpp"

namespace grpcimpl::splatoon::v1 {

async::Task<void> completeSendNotification(grpc::ServerUnaryReactor* reactor, const SendNotificationRequest* request,
                                           const std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonRMC) {
    const nex::rmc::ClientInfo clientInfo = common::deserializeClientInfo(&request->clientinfo());
    const auto type = static_cast<nex::rmc::NotificationType>(request->type());

    if (!co_await splatoonRMC->sendNotification(clientInfo, type, request->srcpid(), request->param1(), request->param2(),
        request->strparam(), request->param3(), true)) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to send notification"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* SplatoonServiceImpl::SendNotification(grpc::CallbackServerContext *context,
    const SendNotificationRequest *request, google::protobuf::Empty *_) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
                "[" + std::string(SplatoonService::service_full_name()) + "] "
                "SendNotification called for PID: " + std::to_string(request->clientinfo().pid())
                + ", NotificationType: " + std::to_string(request->type()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeSendNotification(reactor, request, splatoonRMC);
    task.setContext(reactor);
    splatoonRMC->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

async::Task<void> completeRequestProbeInitiationExt(grpc::ServerUnaryReactor* reactor, const ProbeRequest* request,
    const std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonRMC) {

    const nex::rmc::ClientInfo clientInfo = common::deserializeClientInfo(&request->clientinfo());
    auto probe = std::make_unique<nex::rmc::StationURL>(clientInfo.minorVersion);

    std::vector<uint8_t> probeData(request->probe().begin(), request->probe().end());
    probe->decode(probeData);

    if (!co_await splatoonRMC->externalRequestProbeInitiationExt(clientInfo, std::move(probe))) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initiate probe"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* SplatoonServiceImpl::RequestProbeInitiationExt(grpc::CallbackServerContext* context,
    const ProbeRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
                "[" + std::string(SplatoonService::service_full_name()) + "] "
                "RequestProbeInitiationExt called for PID: " + std::to_string(request->clientinfo().pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeRequestProbeInitiationExt(reactor, request, splatoonRMC);
    task.setContext(reactor);
    splatoonRMC->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

} // namespace grpcimpl::splatoon::v1