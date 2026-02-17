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

async::Task<void> completeGetConnectedClientCount(grpc::ServerUnaryReactor* reactor, GetConnectedClientCountResponse* response,
    const std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonRMC) {

    try {
        response->set_count(co_await splatoonRMC->getConnectedClientCount());
    } catch (const std::exception& e) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* SplatoonServiceImpl::GetConnectedClientCount(grpc::CallbackServerContext* context,
    const google::protobuf::Empty* _, GetConnectedClientCountResponse* response) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC, "[" + std::string(SplatoonService::service_full_name()) + "] "
        "GetConnectedClientCount called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeGetConnectedClientCount(reactor, response, splatoonRMC);
    task.setContext(reactor);
    splatoonRMC->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

async::Task<void> completeGetLobbyCount(grpc::ServerUnaryReactor* reactor, GetLobbyCountResponse* response,
    const std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonRMC) {

    try {
        response->set_count(co_await splatoonRMC->getLobbyCount());
    } catch (const std::exception& e) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* SplatoonServiceImpl::GetLobbyCount(grpc::CallbackServerContext* context,
    const google::protobuf::Empty* _, GetLobbyCountResponse* response) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC, "[" + std::string(SplatoonService::service_full_name()) + "] "
        "GetLobbyCount called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeGetLobbyCount(reactor, response, splatoonRMC);
    task.setContext(reactor);
    splatoonRMC->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

async::Task<void> completeGetExisitingLobbies(grpc::ServerUnaryReactor* reactor, GetExistingLobbiesResponse* response,
    const std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonRMC) {

    try {
        std::vector<nex::rmc::SessionInfo> sessions = co_await splatoonRMC->getAllSessions();

        for (auto& session : sessions) {
            Lobby* lobby = response->add_lobbies();
            lobby->set_gid(session.session->id);
            lobby->set_ownerpid(session.session->ownerPid);
            lobby->set_hostpid(session.session->hostPid);
            lobby->set_minparticipants(session.session->minParticipants);
            lobby->set_maxparticipants(session.session->maxParticipants);
            lobby->set_participationpolicy(session.session->participationPolicy);
            lobby->set_policyargument(session.session->policyArgument);
            lobby->set_state(session.session->state);
            lobby->set_description(session.session->description);

            lobby->set_gamemode(session.session->gameMode);
            for (auto& attr : session.session->attributes) lobby->add_attributes(attr);
            lobby->set_openparticipation(session.session->openParticipation);
            lobby->set_matchmakesystemtype(session.session->matchmakeSystemType);
            lobby->set_progressscore(session.session->progressScore);
            lobby->set_option0(session.session->option0);
            lobby->set_userpasswordenabled(session.session->userPasswordEnabled);
            lobby->set_systempasswordenabled(session.session->systemPasswordEnabled);

            lobby->mutable_startedtime()->set_seconds(
                std::chrono::duration_cast<std::chrono::seconds>(static_cast<std::chrono::system_clock::time_point>(
                    session.session->startedTime).time_since_epoch()).count());
            lobby->mutable_startedtime()->set_nanos(0);

            for (auto& playerPid : session.players) lobby->add_playerpids(playerPid);
        }
    } catch (const std::exception& e) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor *SplatoonServiceImpl::GetExistingLobbies(grpc::CallbackServerContext* context,
    const google::protobuf::Empty* _, GetExistingLobbiesResponse* response) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC, "[" + std::string(SplatoonService::service_full_name()) + "] "
        "GetExistingLobbies called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeGetExisitingLobbies(reactor, response, splatoonRMC);
    task.setContext(reactor);
    splatoonRMC->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

} // namespace grpcimpl::splatoon::v1