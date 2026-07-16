#include "splatoonSecure.hpp"
#include "../types/common/result.hpp"
#include "../types/splatoonSecure/playingSession.hpp"
#include "../../crypto/tools.hpp"
#include "../../grpc/services/common.hpp"
#include "../../grpc/asyncRequest.hpp"

#include <random>
#include <splatoon.pb.h>

#include "../../grpc/services/splatoonService.hpp"
#include "../types/splatoonSecure/competitionRankingScoreInfo.hpp"

namespace nex::rmc {

using namespace async;

Task<std::optional<SplatoonRegisteredClientInfo>> getSplatoonRegisteredClientInfo(
    const std::shared_ptr<ss::SharedState>& sharedState, uint32_t pid) {
    auto [result, info] = co_await sharedState->getSplatoonRegisteredClientInfo(pid);
    if (result != ss::Result::SUCCESS || !info.has_value()) {
        co_return std::nullopt;
    }

    co_return std::move(info);
}

Task<std::optional<SessionInfo>> getSplatoonMatchmakeSession(
    const std::shared_ptr<ss::SharedState>& sharedState, uint32_t gId) {
    auto [result, sessionInfo] = co_await sharedState->getSplatoonMatchmakeSession(gId);
    if (result != ss::Result::SUCCESS || !sessionInfo.has_value()) {
        co_return std::nullopt;
    }

    co_return std::move(sessionInfo);
}

Task<std::optional<std::unordered_map<uint32_t, SessionInfo>>> getAllSplatoonMatchmakeSessions(
    const std::shared_ptr<ss::SharedState>& sharedState) {
    auto [result, sessions] = co_await sharedState->getAllSplatoonMatchmakeSessions();
    if (result != ss::Result::SUCCESS) {
        co_return std::nullopt;
    }

    co_return std::move(sessions);
}

SplatoonSecureRMC::SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                                     std::shared_ptr<ss::SharedState> sharedState, uint32_t serverId,
                                     int gRCPPoolMaxSize, int gRCPRequestTimeout,
                                     const std::shared_ptr<grpc::ChannelCredentials>& grpcCredentials):
                                     Server(std::move(logger), serverId), db(std::move(db)),
                                     sharedState(std::move(sharedState)), gRCPRequestTimeout(gRCPRequestTimeout) {
    logGroup = Logger::group::SPLATOON_SECURE;

    channelPool = std::make_shared<grpcimpl::ChannelPool>(gRCPPoolMaxSize, grpcCredentials);

    // Protocol 3 - NAT Traversal
    REGISTER_CALL(SplatoonSecureRMC::requestProbeInitiationExt, 3, 3);
    REGISTER_CALL(SplatoonSecureRMC::reportNatTraversalResult, 3, 4);
    REGISTER_CALL(SplatoonSecureRMC::reportNatProperties, 3, 5);

    // Protocol 11 - Secure connection
    REGISTER_CALL(SplatoonSecureRMC::secure_register, 11, 1);
    REGISTER_CALL(SplatoonSecureRMC::replaceUrl, 11, 7);
    REGISTER_CALL(SplatoonSecureRMC::sendReport, 11, 8);
    // Game has RequestConnectionData in its code, which given a PID returns the StationURL. Will not implement
    // for now since it does not seem to be used

    // Protocol 21 - Matchmaking
    REGISTER_CALL(SplatoonSecureRMC::unregisterGathering, 21, 2);
    REGISTER_CALL(SplatoonSecureRMC::findBySingleId, 21, 21);
    REGISTER_CALL(SplatoonSecureRMC::getSessionUrls, 21, 41);
    REGISTER_CALL(SplatoonSecureRMC::updateSessionHost, 21, 42);
    REGISTER_CALL(SplatoonSecureRMC::migrateGatheringOwnership, 21, 44);

    // Protocol 50 - Matchmaking (Extension)
    REGISTER_CALL(SplatoonSecureRMC::endParticipation, 50, 1);

    // Protocol 109 - Matchmake Extension
    REGISTER_CALL(SplatoonSecureRMC::closeParticipation, 109, 1);
    REGISTER_CALL(SplatoonSecureRMC::openParticipation, 109, 2);
    REGISTER_CALL(SplatoonSecureRMC::modifyCurrentGameAttribute, 109, 8);
    REGISTER_CALL(SplatoonSecureRMC::getPlayingSessions, 109, 16);
    REGISTER_CALL(SplatoonSecureRMC::updateProgressScore, 109, 34);
    REGISTER_CALL(SplatoonSecureRMC::createMatchmakeSessionWithParam, 109, 38);
    REGISTER_CALL(SplatoonSecureRMC::joinMatchmakeSessionWithParam, 109, 39);
    REGISTER_CALL(SplatoonSecureRMC::autoMatchmakeWithParam_Postpone, 109, 40);
    // TODO Game has FindMatchmakeSessionByGatheringIdDetail, which is just returning the MatchmakeSession given the gId

    // Protocol 112 - Ranking
    REGISTER_CALL(SplatoonSecureRMC::getCompetitionRankingScore, 112, 16);
    REGISTER_CALL(SplatoonSecureRMC::uploadCompetitionRankingScore, 112, 18);
}

Task<void> SplatoonSecureRMC::requestProbeInitiationExt(ClientInfo client, Request req,
                                                        std::unique_ptr<List<StationURL>> targets,
                                                        std::unique_ptr<StationURL> probe) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    for (auto& target : *targets) {
        if (!target.PID.has_value()) {
            res.success = false;
            res.error = Error::CORE__INVALID_ARGUMENT;
            sendMsg(client, res, {});
            co_return;
        }

        auto targetClientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, target.PID.value());
        if (!targetClientInfo.has_value()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        if (client.serverId == serverId) {
            Request probeReq;
            probeReq.protocolId = 3;
            probeReq.methodId = 2; // InitiateProbe
            probeReq.extendedProtocolId = 0;
            std::unique_lock reqCallIdLock(reqCallIdMutex);
            probeReq.callId = nextReqCallId++;
            reqCallIdLock.unlock();

            std::vector<T_ptr> probeParams(1);
            probeParams[0] = std::move(probe);

            sendMsg(targetClientInfo->client, probeReq, probeParams);
        } else {
            auto serverGRPCAddrResult = co_await sharedState->getPublicFacingRPCAddress(client.serverId);
            if (serverGRPCAddrResult.first != ss::Result::SUCCESS || !serverGRPCAddrResult.second.has_value()) {
                logger->log(Logger::level::WARN, logGroup, "Failed to get RPC address for server " + std::to_string(client.serverId)
                                                           + " to send notification to " + std::to_string(client.pid));
                res.success = false;
                res.error = Error::CORE__EXCEPTION;
                sendMsg(client, res, {});
                co_return;
            }

            std::string serverGRPCAddr = serverGRPCAddrResult.second.value();
            auto channel = channelPool->getChannel(serverGRPCAddr);
            if (!channel) {
                logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                                 "Failed to get channel for server " + std::to_string(client.serverId) + " to send notification to " + std::to_string(client.pid));
                res.success = false;
                res.error = Error::CORE__EXCEPTION;
                sendMsg(client, res, {});
                co_return;
            }

            auto request = std::make_shared<grpcimpl::splatoon::v1::ProbeRequest>();
            grpcimpl::common::serializeClientInfo(client, request->mutable_clientinfo());

            std::vector<uint8_t> probeBytes = probe->encode();
            request->set_probe(probeBytes.data(), probeBytes.size());

            auto stub = grpcimpl::splatoon::v1::SplatoonService::NewStub(channel);

            std::pair<std::shared_ptr<google::protobuf::Empty>, grpc::Status> response =
                co_await grpcimpl::callAsync<
                    grpcimpl::splatoon::v1::SplatoonService::Stub,
                    void (grpcimpl::splatoon::v1::SplatoonService::Stub::async::*)(
                        grpc::ClientContext*,
                        const grpcimpl::splatoon::v1::ProbeRequest*,
                        google::protobuf::Empty*,
                        std::function<void(grpc::Status)>
                    ),
                    grpcimpl::splatoon::v1::ProbeRequest,
                    google::protobuf::Empty
                >(
                    stub,
                    &grpcimpl::splatoon::v1::SplatoonService::Stub::async::RequestProbeInitiationExt,
                    std::move(request),
                    gRCPRequestTimeout
                );

            if (!response.second.ok()) {
                logger->log(Logger::level::WARN, logGroup, "Failed to send notification to " + std::to_string(client.pid)
                                                           + " on server " + std::to_string(client.serverId)
                                                           + ": " + response.second.error_message());
                res.success = false;
                res.error = Error::CORE__EXCEPTION;
                sendMsg(client, res, {});
                co_return;
            }
        }
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::reportNatTraversalResult(ClientInfo client, Request req,
                                                       std::unique_ptr<UInt32> cid,
                                                       std::unique_ptr<Bool> result,
                                                       std::unique_ptr<UInt32> rtt) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    logger->log(Logger::level::DEBUG, logGroup, "Received NAT traversal result from " + std::to_string(client.pid) + ", success: " + std::to_string(*result) + ".");

    sendMsg(client, res, {});
    co_return;
}

Task<void> SplatoonSecureRMC::reportNatProperties(ClientInfo client, Request req,
                                                  std::unique_ptr<UInt32> mapping,
                                                  std::unique_ptr<UInt32> filtering,
                                                  std::unique_ptr<UInt32> rtt) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, client.pid);
    if (!clientInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    const auto updateResult = co_await sharedState->updateSplatoonRegisteredClientLastReportedNATProperties(
        client.pid, NATProperties{*mapping, *filtering, *rtt});
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup,
                    "Failed to update NAT properties in shared state for PID " + std::to_string(client.pid));
        sendMsg(client, createError(req, Error::CORE__EXCEPTION), {});
        co_return;
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::secure_register(ClientInfo client, Request req,
                                              std::unique_ptr<List<StationURL>> urls) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(3);

    std::unique_ptr<Result> retval = std::make_unique<Result>();
    retval->success = true;
    retval->code = Error::CORE__UNKNOWN; // This means success

    if (urls->size() != 1) {
        retval->success = false;
        retval->code = Error::CORE__INVALID_ARGUMENT;

        params[0] = std::move(retval);
        params[1] = std::make_unique<UInt32>();
        params[2] = std::make_unique<StationURL>();

        sendMsg(client, res, params);
        co_return;
    }

    std::unique_ptr<StationURL> urlPublic = std::make_unique<StationURL>();
    urlPublic->proto = Protocol::PRUDP;
    urlPublic->ip = client.address.address;
    urlPublic->port = client.address.address.port;
    urlPublic->natf = 0;
    urlPublic->natm = 0;
    urlPublic->pmp = 0;
    urlPublic->sid = 15;
    urlPublic->type = 3;
    urlPublic->upnp = 0;

    std::unique_lock rvConnIdLock(rvConnIdMutex);
    params[0] = std::move(retval);
    params[1] = std::make_unique<UInt32>(0, nextRVConnId);

    (*urls)[0].RVCID = nextRVConnId;

    auto clientInfo = SplatoonRegisteredClientInfo();
    clientInfo.client = client;
    clientInfo.urls = std::vector(urls->begin(), urls->end());
    clientInfo.publicUrl = *urlPublic;
    clientInfo.rvConnId = nextRVConnId;

    params[2] = std::move(urlPublic);

    nextRVConnId++;
    if (nextRVConnId == 0) nextRVConnId++; // 0 is not a valid RVConnID
    rvConnIdLock.unlock();

    const auto setResult = co_await sharedState->setSplatoonRegisteredClientInfo(std::move(clientInfo));
    if (setResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup,
                    "Failed to store registered client in shared state for PID " + std::to_string(client.pid));
        sendMsg(client, createError(req, Error::CORE__EXCEPTION), {});
        co_return;
    }

    sendMsg(client, res, params);
}

Task<void> SplatoonSecureRMC::replaceUrl(ClientInfo client, Request req,
                                         std::unique_ptr<StationURL> oldUrl,
                                         std::unique_ptr<StationURL> newUrl) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, client.pid);
    if (!clientInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    auto updatedURLs = clientInfo->urls;
    const auto urlIt = std::ranges::find(updatedURLs, *oldUrl);
    if (urlIt == updatedURLs.end()) {
        sendMsg(client, res, {});
        co_return;
    }
    *urlIt = *newUrl;

    const auto updateResult = co_await sharedState->updateSplatoonRegisteredClientURLs(client.pid, updatedURLs);
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup,
                    "Failed to update client URLs in shared state for PID " + std::to_string(client.pid));
        sendMsg(client, createError(req, Error::CORE__EXCEPTION), {});
        co_return;
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::sendReport(ClientInfo client, Request req,
                                         std::unique_ptr<UInt32> id,
                                         std::unique_ptr<qBuffer> report) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    // We'll just ignore the report for now. It has a header and a payload that is zlib compressed and then encrypted
    // with AES-ECB, with key 901edf193dc5ef3c5290647bff20c385.

    /*
    // Log the report
    std::stringstream reportStream;
    for (auto& byte : report->data) {
        // Print the hex value of the byte
        reportStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::string reportStr = reportStream.str();
    std::cout << "Report: " << reportStr << std::endl;
    */

    sendMsg(client, res, {});
    co_return;
}

Task<void> SplatoonSecureRMC::unregisterGathering(ClientInfo client, Request req,
                                                  std::unique_ptr<UInt32> gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(1);

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value() || sessionInfo->session->ownerPid != client.pid) {
        params[0] = std::make_unique<Bool>(0, false);
        sendMsg(client, res, params);
        co_return;
    }

    co_await unregisterGathering_internal(*gId, client.pid);

    params[0] = std::make_unique<Bool>(0, true);
    sendMsg(client, res, params);
}

Task<void> SplatoonSecureRMC::findBySingleId(ClientInfo client, Request req,
                                             std::unique_ptr<UInt32> id) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(2);

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *id);
    if (!sessionInfo.has_value()) {
        params[0] = std::make_unique<Bool>(0, false);

        Gathering gathering(client.minorVersion);
        std::unique_ptr<AnyDataHolder> data = std::make_unique<AnyDataHolder>(client.minorVersion);
        data->set(gathering, "Gathering");
        params[1] = std::move(data);

        sendMsg(client, res, params);
        co_return;
    }

    params[0] = std::make_unique<Bool>(0, true);

    Gathering gathering(client.minorVersion);
    gathering = static_cast<Gathering>(*sessionInfo->session);

    std::unique_ptr<AnyDataHolder> data = std::make_unique<AnyDataHolder>(client.minorVersion);
    data->set(gathering, "Gathering");
    params[1] = std::move(data);

    sendMsg(client, res, params);
}

Task<void> SplatoonSecureRMC::getSessionUrls(ClientInfo client, Request req,
                                             std::unique_ptr<UInt32> gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(1);

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, params);
        co_return;
    }

    std::unique_ptr<List<StationURL>> urls = std::make_unique<List<StationURL>>(client.minorVersion);
    const uint32_t hostPid = sessionInfo->session->hostPid;
    auto hostClientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, hostPid);
    if (!hostClientInfo.has_value() || hostClientInfo->urls.empty()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
        sendMsg(client, res, {});
        co_return;
    }

    StationURL internalPublicUrl = hostClientInfo->urls[0];
    StationURL hostPublicUrl = hostClientInfo->publicUrl;
    uint32_t hostRVCID = hostClientInfo->rvConnId;

    internalPublicUrl.RVCID = hostRVCID;
    hostPublicUrl.RVCID = hostRVCID;
    internalPublicUrl.PID = hostPid;
    hostPublicUrl.PID = hostPid;

    urls->push_back(std::move(internalPublicUrl));
    urls->push_back(std::move(hostPublicUrl));

    params[0] = std::move(urls);

    sendMsg(client, res, params);
}

Task<void> SplatoonSecureRMC::updateSessionHost(ClientInfo client, Request req,
                                                std::unique_ptr<UInt32> gId,
                                                std::unique_ptr<Bool> migrateOwner) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value()) {
        logger->log(Logger::level::DEBUG, logGroup, "Update session host failed: Invalid GID " + std::to_string(*gId) + " for client " + std::to_string(client.pid) + ".");
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (!sessionInfo->players.contains(client.pid)) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionInfo->session->hostPid = client.pid;
    if (migrateOwner) sessionInfo->session->ownerPid = client.pid;

    const auto updateResult = co_await sharedState->updateSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update session host in shared state for GID " + std::to_string(*gId) + ".");
    }

    sendMsg(client, res, {});

    for (auto& pid : sessionInfo->players) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) continue;

        co_await sendNotification(playerInfo->client, NotificationType::HOST_CHANGED,
                         client.pid, sessionInfo->session->id, client.pid, "", 0);

        if (migrateOwner) {
            uint64_t msNow = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            co_await sendNotification(playerInfo->client, NotificationType::OWNERSHIP_CHANGED,
                             client.pid, sessionInfo->session->id, client.pid,
                             std::to_string(msNow), 0);
        }
    }
}

Task<void> SplatoonSecureRMC::migrateGatheringOwnership(ClientInfo client, Request req,
                                                        std::unique_ptr<UInt32> gId,
                                                        std::unique_ptr<List<PID>> potentialNewOwners,
                                                        std::unique_ptr<Bool> participantsOnly) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value()) {
        logger->log(Logger::level::DEBUG, logGroup, "Update session host failed: Invalid GID " + std::to_string(*gId) + " for client " + std::to_string(client.pid) + ".");
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (!sessionInfo->players.contains(client.pid)) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    std::set<uint32_t> consideredOwners;

    for (const auto& pid: *potentialNewOwners) {
        if (!participantsOnly || sessionInfo->players.contains(pid)) {
            consideredOwners.insert(pid);
        }
    }

    // Also add the session players
    for (const auto& pid : sessionInfo->players) {
        consideredOwners.insert(pid);
    }

    // TODO Choose best owner based on some criteria, for now just pick the first one
    if (consideredOwners.empty()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__SESSION_VOID; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    uint32_t newOwnerPid = *consideredOwners.begin();
    logger->log(Logger::level::DEBUG, logGroup, "Migrating ownership of session " + std::to_string(*gId) + " to " + std::to_string(newOwnerPid) + " for client " + std::to_string(client.pid) + ".");
    sessionInfo->session->ownerPid = newOwnerPid;

    const auto updateResult = co_await sharedState->updateSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update session owner in shared state for GID " + std::to_string(*gId) + ".");
    }

    for (const auto& pid : sessionInfo->players) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) continue;

        co_await sendNotification(playerInfo->client, NotificationType::OWNERSHIP_CHANGED,
                         client.pid, sessionInfo->session->id, newOwnerPid, "", 0);
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::endParticipation(ClientInfo client, Request req,
                                               std::unique_ptr<UInt32> gId,
                                               std::unique_ptr<String> msg) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(1);

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value() || !sessionInfo->players.contains(client.pid)) {
        params[0] = std::make_unique<Bool>(0, false);
        sendMsg(client, res, {});
        co_return;
    }

    co_await removePlayerFromSession(*gId, client.pid, *msg);

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::closeParticipation(ClientInfo client, Request req,
                                                 std::unique_ptr<UInt32> gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionInfo->session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionInfo->session->openParticipation = false;
    const auto updateResult = co_await sharedState->updateSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to close participation in shared state for GID " + std::to_string(*gId) + ".");
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::openParticipation(ClientInfo client, Request req,
                                                std::unique_ptr<UInt32> gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionInfo->session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionInfo->session->openParticipation = true;
    const auto updateResult = co_await sharedState->updateSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to open participation in shared state for GID " + std::to_string(*gId) + ".");
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::modifyCurrentGameAttribute(ClientInfo client, Request req,
                                                         std::unique_ptr<UInt32> gId,
                                                         std::unique_ptr<UInt32> attribIndex,
                                                         std::unique_ptr<UInt32> newValue) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionInfo->session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    if (*attribIndex < static_cast<uint32_t>(sessionInfo->session->attributes.size())) {
        logger->log(Logger::level::DEBUG, logGroup, "Modifying attribute " + std::to_string(*attribIndex) + " to " + std::to_string(*newValue) + " in session " + std::to_string(*gId) + " owned by " + std::to_string(client.pid) + ".");
        // sessionIt->second.session->attributes[attribIndex] = newValue;
    }
    const auto updateResult = co_await sharedState->updateSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to modify game attributes in shared state for GID " + std::to_string(*gId) + ".");
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::getPlayingSessions(ClientInfo client, Request req,
                                                 std::unique_ptr<List<PID>> pids) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::unique_ptr<List<PlayingSession>> sessions = std::make_unique<List<PlayingSession>>(client.minorVersion);
    for (const auto& pid : *pids) {
        auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!clientInfo.has_value()) continue;

        for (const auto& existingSession : clientInfo->joinedGatherings) {
            PlayingSession session(client.minorVersion);
            session.pid = pid;
            session.gathering = *existingSession;

            sessions->push_back(std::move(session));
        }
    }

    std::vector<T_ptr> params(1);
    params[0] = std::move(sessions);

    sendMsg(client, res, params);
    co_return;
}

Task<void> SplatoonSecureRMC::updateProgressScore(ClientInfo client, Request req,
                                                  std::unique_ptr<UInt32> gId,
                                                  std::unique_ptr<UInt8> score) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, *gId);
    if (!sessionInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionInfo->session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionInfo->session->progressScore = *score;
    const auto updateResult = co_await sharedState->updateSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (updateResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to update progress score in shared state for GID " + std::to_string(*gId) + ".");
    }

    sendMsg(client, res, {});
}

Task<void> SplatoonSecureRMC::createMatchmakeSessionWithParam(ClientInfo client, Request req,
                                                              std::unique_ptr<CreateMatchmakeSessionParam> param) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, client.pid);
    if (!clientInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED;
        sendMsg(client, res, {});
        co_return;
    }

    if (static_cast<uint32_t>(param->srcMatchmakeSession.maxParticipants) < param->additionalParticipants.size() + 1) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__SESSION_FULL; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    logger->log(Logger::level::DEBUG, logGroup, "Creating matchmake session with " + std::to_string(param->additionalParticipants.size()) + " additional participants.");

    std::vector<uint32_t> playerPids;
    playerPids.push_back(client.pid);
    for (auto& pid : param->additionalParticipants) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        playerPids.push_back(pid);
    }

    std::shared_ptr<MatchmakeSession> session = std::make_shared<MatchmakeSession>(param->srcMatchmakeSession);
    session->id = co_await getNewGatheringId();
    session->ownerPid = client.pid;
    session->hostPid = client.pid;
    session->participationCount = static_cast<uint32_t>(param->additionalParticipants.size()) + 1;
    session->openParticipation = true;
    session->sessionKey = Buffer(crypto::genKey());
    session->startedTime = std::chrono::system_clock::now();

    if (!session->userPassword.empty()) session->userPasswordEnabled = true; // Why does the client not set this?

    logger->log(Logger::level::DEBUG, logGroup, "Matchmake session created with ID " + std::to_string(session->id) + " for " + std::to_string(client.pid) + ":\n" + session->toString());

    // All checks passed, add players to session and send success
    SessionInfo sessionInfo{session, std::set<uint32_t>(playerPids.begin(), playerPids.end())};
    const auto setResult = co_await sharedState->setSplatoonMatchmakeSession(std::move(sessionInfo));
    if (setResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to store new matchmake session in shared state for GID " + std::to_string(session->id) + ".");
        sendMsg(client, createError(req, Error::CORE__EXCEPTION), {});
        co_return;
    }

    session->minorVersion = client.minorVersion;
    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<MatchmakeSession>(*session);

    sendMsg(client, res, params);

    // Send notifications to all players
    // FIXME Actually send only to owner
    for (auto& pid : playerPids) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) continue;

        for (auto& playerPid : playerPids) {
            co_await sendNotification(playerInfo->client, NotificationType::NEW_PARTICIPANT,
                             client.pid, session->id, playerPid, param->joinMessage, 1);
        }
    }
}

Task<void> SplatoonSecureRMC::joinMatchmakeSessionWithParam(ClientInfo client, Request req,
                                                            std::unique_ptr<JoinMatchmakeSessionParam> param) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, client.pid);
    if (!clientInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED;
        sendMsg(client, res, {});
        co_return;
    }

    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, param->gid);
    if (!sessionInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    auto& session = sessionInfo->session;
    if (static_cast<uint32_t>(session->participationCount) + param->additionalParticipants.size() >
            static_cast<uint16_t>(session->maxParticipants)) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__SESSION_FULL;
        sendMsg(client, res, {});
        co_return;
    }

    if (!session->openParticipation) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__SESSION_CLOSED;
        sendMsg(client, res, {});
        co_return;
    }

    if (session->userPasswordEnabled && param->userPassword != session->userPassword) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__MATCHMAKE_SESSION_USER_PASSWORD_UNMATCH;
        sendMsg(client, res, {});
        co_return;
    }

    std::vector<uint32_t> playerPids;
    playerPids.push_back(client.pid);
    for (auto& pid : param->additionalParticipants) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        playerPids.push_back(pid);
    }

    // All checks passed, add players to session and send success
    sessionInfo->session->participationCount += static_cast<uint32_t>(playerPids.size());

    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<MatchmakeSession>(*sessionInfo->session);
    params[0]->minorVersion = client.minorVersion;

    sendMsg(client, res, params);

    const auto existingPlayers = sessionInfo->players;
    for (auto& pid : playerPids) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) continue;

        for (auto& existingPlayerPid : existingPlayers) {
            auto existingPlayerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, existingPlayerPid);
            if (!existingPlayerInfo.has_value()) continue;

            co_await sendNotification(existingPlayerInfo->client, NotificationType::NEW_PARTICIPANT,
                             client.pid, param->gid, pid, param->joinMessage, 1);
        }
    }

    sessionInfo->players.insert(playerPids.begin(), playerPids.end());
    const auto setResult = co_await sharedState->setSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (setResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to persist joined matchmake session in shared state for GID " + std::to_string(param->gid) + ".");
    }

    for (auto& pid : playerPids) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) continue;

        for (auto& existingPlayerPid : sessionInfo->players) {
            co_await sendNotification(playerInfo->client, NotificationType::NEW_PARTICIPANT,
                             client.pid, param->gid, existingPlayerPid, param->joinMessage, 1);
        }
    }
}

Task<void> SplatoonSecureRMC::autoMatchmakeWithParam_Postpone(ClientInfo client, Request req,
                                                              std::unique_ptr<AutoMatchmakeParam> param) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, client.pid);
    if (!clientInfo.has_value()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    if (param->gidForParitipationCheck != UInt32(0)) {
        // Check that the client is already in the gathering
        auto sessionInfoForCheck = co_await getSplatoonMatchmakeSession(sharedState, param->gidForParitipationCheck);
        if (!sessionInfoForCheck.has_value()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__INVALID_GID;
            sendMsg(client, res, {});
            co_return;
        }

        if (!sessionInfoForCheck->players.contains(client.pid)) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
            sendMsg(client, res, {});
            co_return;
        }
    }

    for (auto& additionalPlayer : param->additionalParticipants) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, additionalPlayer);
        if (!playerInfo.has_value()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        bool foundGathering = false;

        for (auto& clientGathering : clientInfo->joinedGatherings) {
            for (auto& playerGathering : playerInfo->joinedGatherings) {
                if (clientGathering->id == playerGathering->id) {
                    foundGathering = true;
                    break;
                }
            }
        }

        if (!foundGathering) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
            sendMsg(client, res, {});
            co_return;
        }
    }

    logger->log(Logger::level::DEBUG, logGroup, "AutoMatchmakeParam from " + std::to_string(client.pid) + ":\n" + param->toString());

    auto allSessions = co_await getAllSplatoonMatchmakeSessions(sharedState);
    if (!allSessions.has_value()) {
        sendMsg(client, createError(req, Error::CORE__EXCEPTION), {});
        co_return;
    }

    // Log all the sessions that are currently available for matchmaking
    logger->log(Logger::level::DEBUG, logGroup, "Current matchmake sessions:");
    for (const auto &[session, players]: *allSessions | std::views::values) {
        logger->log(Logger::level::DEBUG, logGroup, session->toString());
    }

    auto filter = [&](const SessionInfo& sessionInfo) -> bool {
        bool valid = true;

        // Filter out sessions that are already joined by the client
        for (auto& clientGathering : clientInfo->joinedGatherings) {
            if (clientGathering->id == sessionInfo.session->id) {
                valid = false;
                break;
            }
        }

        // Filter out sessions that are not open for participation or don't have enough space
        if (!sessionInfo.session->openParticipation) valid = false;
        const uint32_t vacantParticipants = param->additionalParticipants.size() + 1;
        if (uint32_t maxAllowedExistingPlayers = static_cast<uint32_t>(sessionInfo.session->maxParticipants) - vacantParticipants; sessionInfo.session->participationCount > maxAllowedExistingPlayers) valid = false;

        if (!valid) return false;

        bool sessionValid = false;
        for (int i = 0; i < param->searchCriteria.size() && !sessionValid; i++) {
            sessionValid = true;
            auto& criteria = param->searchCriteria[i];
            if (criteria.attributes.size() != sessionInfo.session->attributes.size()) {
                sessionValid = false;
                continue;
            }

            for (int j = 0; j < criteria.attributes.size() && sessionValid; j++) {
                auto& attribute = criteria.attributes[j];
                uint32_t attrValue = std::stoi(attribute);
                if (j == 1) continue; // j == 1 is the player exp (not the one shown in game), so it can differ.
                if (attrValue != sessionInfo.session->attributes[j]) sessionValid = false;
            }

            if (static_cast<uint32_t>(std::stoi(criteria.gameMode)) != sessionInfo.session->gameMode) sessionValid = false;

            auto minParticipants = static_cast<std::string>(criteria.minParticipants);
            if (minParticipants.find(',') == std::string::npos) {
                throw std::logic_error("minParticipants does not contain a comma");
            }
            uint16_t min_minParticipants = std::stoi(minParticipants.substr(0, minParticipants.find(',')));
            uint16_t max_minParticipants = std::stoi(minParticipants.substr(minParticipants.find(',') + 1));

            auto maxParticipants = static_cast<std::string>(criteria.maxParticipants);
            if (maxParticipants.find(',') == std::string::npos) {
                throw std::logic_error("maxParticipants does not contain a comma");
            }
            uint16_t min_maxParticipants = std::stoi(maxParticipants.substr(0, maxParticipants.find(',')));
            uint16_t max_maxParticipants = std::stoi(maxParticipants.substr(maxParticipants.find(',') + 1));

            if (sessionInfo.session->minParticipants < min_minParticipants || sessionInfo.session->minParticipants > max_minParticipants) sessionValid = false;
            if (sessionInfo.session->maxParticipants < min_maxParticipants || sessionInfo.session->maxParticipants > max_maxParticipants) sessionValid = false;

            if (static_cast<uint32_t>(std::stoi(criteria.matchmakeSystemType)) != sessionInfo.session->matchmakeSystemType) sessionValid = false;
            if (criteria.excludeUserPasswordSet && sessionInfo.session->userPasswordEnabled) sessionValid = false;
            if (criteria.excludeSystemPasswordSet && sessionInfo.session->systemPasswordEnabled) sessionValid = false;
            if (criteria.referGid != sessionInfo.session->referGid) sessionValid = false;

            for (auto& mmParam : criteria.matchmakeParam.params) {
                auto sessionIt = sessionInfo.session->matchmakeParam.params.find(mmParam.first);
                if (sessionIt == sessionInfo.session->matchmakeParam.params.end()) {
                    sessionValid = false;
                    break;
                }

                if (mmParam.second != sessionIt->second) {
                    sessionValid = false;
                    break;
                }
            }
        }

        return sessionValid;
    };

    auto cmp = [&](const std::pair<uint32_t, SessionInfo>& a, const std::pair<uint32_t, SessionInfo>& b) {
        return a.first < b.first;
    };

    std::priority_queue<std::pair<uint32_t, SessionInfo>, std::vector<std::pair<uint32_t, SessionInfo>>, decltype(cmp)> validSessions(cmp);
    for (auto &val: *allSessions | std::views::values) {
        try {
            if (filter(val)) {
                uint32_t sessionExp = val.session->attributes[1];
                uint32_t userExp = param->srcMatchmakeSession.attributes[1];
                validSessions.emplace(static_cast<uint32_t>(abs(static_cast<int64_t>(sessionExp) - userExp)), val);
            }
        } catch (const std::logic_error& e) {
            res.success = false;
            res.error = Error::CORE__INVALID_ARGUMENT;

            logger->log(Logger::level::WARN, logGroup,
                        "Malformed AutoMatchmakeParam from " + util::ipv4ToString(client.address.address) + ": " +
                        std::string(e.what()));

            sendMsg(client, res, {});
            co_return;
        }
    }

    std::vector<T_ptr> params(1);
    SessionInfo sessionInfo;

    if (validSessions.empty()) {
        auto session = std::make_shared<MatchmakeSession>(std::move(param->srcMatchmakeSession));

        session->id = co_await getNewGatheringId();
        session->ownerPid = client.pid;
        session->hostPid = client.pid;
        if (session->gameMode == static_cast<uint32_t>(12)) { // Festival, I don't know why it is set to not open by default
            session->openParticipation = true;
        }
        session->participationCount = 0;
        session->sessionKey = Buffer(crypto::genKey());
        session->startedTime = std::chrono::system_clock::now();

        std::set<uint32_t> players;
        players.insert(client.pid);
        for (auto& pid : param->additionalParticipants) players.insert(pid);

        sessionInfo = {session, players};
        params[0] = std::make_unique<MatchmakeSession>(*session);

        logger->log(Logger::level::INFO, logGroup, "Creating new session " + std::to_string(session->id) + " for " + std::to_string(client.pid) + ":\n" + session->toString());
    } else {
        sessionInfo = validSessions.top().second;
        sessionInfo.players.insert(client.pid);
        for (auto& pid : param->additionalParticipants) sessionInfo.players.insert(pid);
        params[0] = std::make_unique<MatchmakeSession>(*sessionInfo.session);

        logger->log(Logger::level::INFO, logGroup, "Joining existing session " + std::to_string(sessionInfo.session->id) + " for " + std::to_string(client.pid) + ":\n" + sessionInfo.session->toString());
    }

    sessionInfo.session->participationCount += static_cast<uint32_t>(param->additionalParticipants.size()) + 1;
    const uint32_t sessionId = sessionInfo.session->id;
    const auto setResult = co_await sharedState->setSplatoonMatchmakeSession(std::move(sessionInfo));
    if (setResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup, "Failed to persist automatch session in shared state for GID " + std::to_string(sessionId) + ".");
        sendMsg(client, createError(req, Error::CORE__EXCEPTION), {});
        co_return;
    }

    auto persistedSessionInfo = co_await getSplatoonMatchmakeSession(sharedState, sessionId);
    if (!persistedSessionInfo.has_value()) {
        sendMsg(client, createError(req, Error::CORE__EXCEPTION), {});
        co_return;
    }

    params[0]->minorVersion = client.minorVersion;
    sendMsg(client, res, params);

    std::set<uint32_t> newPlayers;
    newPlayers.insert(client.pid);
    for (auto& pid : param->additionalParticipants) newPlayers.insert(pid);

    // Switch the players to the new gathering
    std::set<uint32_t> switchedPlayers;
    for (auto& playerPid : newPlayers) {
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, playerPid);
        if (!playerInfo.has_value()) continue;
        // We don't need to check if the player is online, because it has been checked before

        if (param->gidForParitipationCheck != UInt32(0)) {
            switchedPlayers.insert(playerPid);

            co_await sendNotification(playerInfo->client, NotificationType::SWITCH_GATHERING,
                             client.pid, persistedSessionInfo->session->id, playerPid, "", 1);
        }
    }

    // We notify other players of the new participant(s)
    for (auto& pid : persistedSessionInfo->players) {
//        if (newPlayers.contains(pid)) continue;
        if (persistedSessionInfo->session->ownerPid != pid && client.pid != pid) continue;
        auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!playerInfo.has_value()) continue;
        co_await sendNotification(playerInfo->client, NotificationType::NEW_PARTICIPANT, client.pid,
                         persistedSessionInfo->session->id, client.pid, "", 1);
    }

    // And we also send notifications to the new participant(s), one for each player already in the session (including themselves)
//    for (auto& pid : sessionInfo.players) {
//        for (auto& newPlayerPid : newPlayers) {
//            co_await sendNotification(newPlayerInfo->client, NotificationType::NEW_PARTICIPANT, client.pid,
//                             sessionInfo.session->id, pid, "", 1);
//        }
//    }
}

Task<void> SplatoonSecureRMC::getCompetitionRankingScore(ClientInfo client, Request req,
                                                         std::unique_ptr<CompetitionRankingGetParam> param) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::vector<T_ptr> params(1);

    if (param->festivalIds.size() > 10) {
        res.success = false;
        res.error = Error::CORE__INVALID_ARGUMENT;
        sendMsg(client, res, {});
        co_return;
    }

    std::unique_ptr<List<CompetitionRankingScoreInfo>> scores = std::make_unique<List<CompetitionRankingScoreInfo>>(client.minorVersion);

    for (auto& festivalId : param->festivalIds) {
        CompetitionRankingScoreInfo scoreInfo(client.minorVersion);
        scoreInfo.festivalId = festivalId;
        scoreInfo.unk1 = 0;

        if (db != nullptr) {
            auto cmd = db::Database::craftGetFestivalTotalsCommand(festivalId);
            auto result = co_await db->runCommand(std::move(cmd));
            if (result.getStatus() != db::DBResultStatus::SUCCESS) {
                logger->log(Logger::level::WARN, logGroup,
                            "Failed to get festival totals for festival " + std::to_string(festivalId));
                res.success = false;
                res.error = Error::CORE__EXCEPTION;
                sendMsg(client, res, {});
                co_return;
            }

            auto totalsData = result.getData<std::vector<db::DBFestivalTeamTotalsData>>();

            List<UInt32> teamWins(client.minorVersion);
            List<UInt32> teamVotes(client.minorVersion);
            for (auto& data : totalsData) {
                teamWins.emplace_back(client.minorVersion, data.totalWins);
                teamVotes.emplace_back(client.minorVersion, data.userCount);
            }
            scoreInfo.teamWins = std::move(teamWins);
            scoreInfo.teamVotes = std::move(teamVotes);
        }

        List<CompetitionRankingScoreData> scoreData(client.minorVersion);
        scoreInfo.scoreData = std::move(scoreData);

        scores->push_back(std::move(scoreInfo));
    }

    params[0] = std::move(scores);

    sendMsg(client, res, params);
    co_return;
}

Task<void> SplatoonSecureRMC::uploadCompetitionRankingScore(ClientInfo client, Request req,
                                                            std::unique_ptr<CompetitionRankingUploadScoreParam> param) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    if (db != nullptr) {
        auto cmd = db::Database::craftUploadFestivalScoreCommand(
            param->festivalId, client.pid, static_cast<uint32_t>(param->teamId) == 0 ? 0 : 1, param->teamScore);
        auto result = co_await db->runCommand(std::move(cmd));
        if (result.getStatus() != db::DBResultStatus::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup,
                        "Failed to upload competition ranking score for PID " + std::to_string(client.pid));
            res.success = false;
            res.error = Error::CORE__EXCEPTION;
            sendMsg(client, res, {});
            co_return;
        }
    }

    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<Bool>(client.minorVersion, true);

    sendMsg(client, res, params);
    co_return;
}

Task<void> SplatoonSecureRMC::onDisconnect(prudp::PRUDPAddress address) {
    std::unique_lock pidMapLock(pidMapMutex);
    auto pidIt = pidMap.find(address);
    if (pidIt != pidMap.end()) {
        const uint32_t pid = pidIt->second;
        pidMapLock.unlock();

        auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (clientInfo.has_value()) {
            for (auto& gathering : clientInfo->joinedGatherings) {
            // If the client is in a gathering, we remove them from it
                co_await removePlayerFromSession(gathering->id, pid, "", true);
            }
        }

        const auto deleteResult = co_await sharedState->deleteSplatoonRegisteredClientInfo(pid);
        if (deleteResult != ss::Result::SUCCESS) {
            logger->log(Logger::level::WARN, logGroup,
                        "Failed to delete disconnected client from shared state for PID " + std::to_string(pid));
        }
    } else {
        pidMapLock.unlock();
    }

    Server::onDisconnect(address);
    co_return;
}

Task<uint32_t> SplatoonSecureRMC::getNewGatheringId() {
    // We choose a random number between 1000 and UINT32_MAX, and if it's already taken, we try again.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(1000, UINT32_MAX);

    uint32_t id = dis(gen);

    while (true) {
        auto [getResult, sessionInfo] = co_await sharedState->getSplatoonMatchmakeSession(id);
        if (getResult != ss::Result::SUCCESS || !sessionInfo.has_value()) break;
        id = dis(gen);
    }

    co_return id;
}

Task<bool> SplatoonSecureRMC::sendNotification(ClientInfo client, const NotificationType type, const uint32_t srcPid, const uint32_t param1,
                                               const uint32_t param2, const std::string& strParam, const uint32_t param3, bool dontResend) {
    if (client.serverId == serverId || dontResend) {
        std::unique_lock pidMapLock(pidMapMutex);
        if (pidMap.contains(client.address)) {
            Request notification;
            notification.protocolId = 14;
            notification.methodId = 1;
            notification.extendedProtocolId = 0;
            std::unique_lock reqCallIdLock(reqCallIdMutex);
            notification.callId = nextReqCallId++;
            reqCallIdLock.unlock();

            std::vector<T_ptr> params(1);
            std::unique_ptr<NotificationEvent> event = std::make_unique<NotificationEvent>(client.minorVersion);
            event->type = type;
            event->srcPid = srcPid;
            event->param1 = param1;
            event->param2 = param2;
            event->strParam = strParam;
            event->param3 = param3;

            params[0] = std::move(event);

            sendMsg(client, notification, params);
        } else {
            logger->log(Logger::level::WARN, logGroup, "Failed to send notification to " + std::to_string(client.pid)
                                                       + " because they are not connected");
            co_return false;
        }
    } else {
        auto serverGRPCAddrResult = co_await sharedState->getPublicFacingRPCAddress(client.serverId);
        if (serverGRPCAddrResult.first != ss::Result::SUCCESS || !serverGRPCAddrResult.second.has_value()) {
            logger->log(Logger::level::WARN, logGroup, "Failed to get RPC address for server " + std::to_string(client.serverId)
                                                       + " to send notification to " + std::to_string(client.pid));
            co_return false;
        }

        std::string serverGRPCAddr = serverGRPCAddrResult.second.value();
        auto channel = channelPool->getChannel(serverGRPCAddr);
        if (!channel) {
            logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT,
                             "Failed to get channel for server " + std::to_string(client.serverId) + " to send notification to " + std::to_string(client.pid));
            co_return false;
        }

        auto request = std::make_shared<grpcimpl::splatoon::v1::SendNotificationRequest>();
        grpcimpl::common::serializeClientInfo(client, request->mutable_clientinfo());
        request->set_type(static_cast<grpcimpl::splatoon::v1::NotificationType>(type));
        request->set_srcpid(srcPid);
        request->set_param1(param1);
        request->set_param2(param2);
        request->set_strparam(strParam);
        request->set_param3(param3);

        auto stub = grpcimpl::splatoon::v1::SplatoonService::NewStub(channel);

        std::pair<std::shared_ptr<google::protobuf::Empty>, grpc::Status> response =
            co_await grpcimpl::callAsync<
                grpcimpl::splatoon::v1::SplatoonService::Stub,
                void (grpcimpl::splatoon::v1::SplatoonService::Stub::async::*)(
                    grpc::ClientContext*,
                    const grpcimpl::splatoon::v1::SendNotificationRequest*,
                    google::protobuf::Empty*,
                    std::function<void(grpc::Status)>
                ),
                grpcimpl::splatoon::v1::SendNotificationRequest,
                google::protobuf::Empty
            >(
                stub,
                &grpcimpl::splatoon::v1::SplatoonService::Stub::async::SendNotification,
                std::move(request),
                gRCPRequestTimeout
            );

        if (!response.second.ok()) {
            logger->log(Logger::level::WARN, logGroup, "Failed to send notification to " + std::to_string(client.pid)
                                                       + " on server " + std::to_string(client.serverId)
                                                       + ": " + response.second.error_message());
            co_return false;
        }
    }

    co_return true;
}

Task<void> SplatoonSecureRMC::unregisterGathering_internal(uint32_t gId, uint32_t srcPid) {
    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, gId);
    if (!sessionInfo.has_value()) co_return;

    for (auto& pid : sessionInfo->players) {
        auto clientInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
        if (!clientInfo.has_value()) continue;

        co_await sendNotification(clientInfo->client, NotificationType::GATHERING_UNREGISTERED, srcPid, gId, 0, "", 0);
    }

    const auto deleteResult = co_await sharedState->deleteSplatoonMatchmakeSession(gId);
    if (deleteResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup,
                    "Failed to delete gathering in shared state for GID " + std::to_string(gId));
    }
}

Task<void> SplatoonSecureRMC::removePlayerFromSession(uint32_t gId, uint32_t playerPid, const std::string& msg, bool disconnected) {
    auto sessionInfo = co_await getSplatoonMatchmakeSession(sharedState, gId);
    if (!sessionInfo.has_value()) co_return;

    if (!sessionInfo->players.contains(playerPid)) {
        logger->log(Logger::level::WARN, logGroup, "Player " + std::to_string(playerPid) + " tried to leave session " + std::to_string(gId) + ", but is not in it.");
        co_return;
    }

    auto playerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, playerPid);
    if (!playerInfo.has_value()) {
        logger->log(Logger::level::WARN, logGroup, "Player " + std::to_string(playerPid) + " tried to leave session " + std::to_string(gId) + ", but is not registered.");
        co_return;
    }

    if (!disconnected) {
        co_await sendNotification(playerInfo->client, NotificationType::PARTICIPATION_ENDED,
                         playerPid, sessionInfo->session->id, playerPid, msg, 0);
    }

    sessionInfo->players.erase(playerPid);
    sessionInfo->session->participationCount = static_cast<uint32_t>(sessionInfo->players.size());

    if (sessionInfo->players.empty()) {
        co_await unregisterGathering_internal(gId, playerPid);
        co_return;
    }

    uint32_t ownerPid = sessionInfo->session->ownerPid;
    auto ownerInfo = co_await getSplatoonRegisteredClientInfo(sharedState, ownerPid);
    if (ownerInfo.has_value()) {
        co_await sendNotification(ownerInfo->client, disconnected ? NotificationType::PARTICIPANT_DISCONNECTED : NotificationType::PARTICIPATION_ENDED,
                         playerPid, sessionInfo->session->id, playerPid, msg, 0);
    }

    if (sessionInfo->session->ownerPid == playerPid) {
        // TODO Choose the best player as the new owner
        sessionInfo->session->ownerPid = *sessionInfo->players.begin();
        for (auto& pid : sessionInfo->players) {
            uint64_t msNow = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            auto participantInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
            if (!participantInfo.has_value()) continue;
            co_await sendNotification(participantInfo->client, NotificationType::OWNERSHIP_CHANGED, playerPid,
                             sessionInfo->session->id, sessionInfo->session->ownerPid, std::to_string(msNow), 0);
        }
    }

    if (sessionInfo->session->hostPid == playerPid) {
        // TODO Choose the best player as the new host
        sessionInfo->session->hostPid = *sessionInfo->players.begin();
        for (auto& pid : sessionInfo->players) {
            auto participantInfo = co_await getSplatoonRegisteredClientInfo(sharedState, pid);
            if (!participantInfo.has_value()) continue;
            co_await sendNotification(participantInfo->client, NotificationType::HOST_CHANGED, playerPid,
                             sessionInfo->session->id, sessionInfo->session->hostPid, "", 0);
        }
    }

    const auto setResult = co_await sharedState->setSplatoonMatchmakeSession(std::move(*sessionInfo));
    if (setResult != ss::Result::SUCCESS) {
        logger->log(Logger::level::WARN, logGroup,
                    "Failed to update session while removing player in shared state for GID " + std::to_string(gId));
    }
}

Task<bool> SplatoonSecureRMC::externalRequestProbeInitiationExt(const ClientInfo client, std::unique_ptr<StationURL> probe) {
    if (client.serverId != serverId) {
        logger->log(Logger::level::WARN, logGroup, "Failed to send probe initiation request to " + std::to_string(client.pid)
                                                   + " because they are not connected to this server.");
        co_return false;
    }

    Request probeReq;
    probeReq.protocolId = 3;
    probeReq.methodId = 2; // InitiateProbe
    probeReq.extendedProtocolId = 0;
    std::unique_lock reqCallIdLock(reqCallIdMutex);
    probeReq.callId = nextReqCallId++;
    reqCallIdLock.unlock();

    std::vector<T_ptr> probeParams(1);
    probeParams[0] = std::move(probe);

    sendMsg(client, probeReq, probeParams);
    co_return true;
}

Task<uint32_t> SplatoonSecureRMC::getConnectedClientCount() const {
    auto [result, count] = co_await sharedState->getSplatoonRegisteredClientCount();

    if (result != ss::Result::SUCCESS) {
        throw std::runtime_error("Failed to get connected client count.");
    }

    co_return count;
}

Task<uint32_t> SplatoonSecureRMC::getLobbyCount() const {
    auto [result, count] = co_await sharedState->getSplatoonMatchmakeSessionCount();

    if (result != ss::Result::SUCCESS) {
        throw std::runtime_error("Failed to get lobby count.");
    }

    co_return count;
}

Task<std::vector<SessionInfo>> SplatoonSecureRMC::getAllSessions() const {
    auto [result, sessions] = co_await sharedState->getAllSplatoonMatchmakeSessions();

    if (result != ss::Result::SUCCESS) {
        throw std::runtime_error("Failed to get sessions.");
    }

    std::vector<SessionInfo> sessionVec;
    for (const auto &sessionInfo: sessions | std::views::values) {
        sessionVec.push_back(sessionInfo);
    }

    co_return sessionVec;
}

Task<std::vector<db::DBFestivalTeamTotalsData>> SplatoonSecureRMC::getFestivalTotals(uint32_t festivalId) const {
    if (db == nullptr) {
        co_return {};
    }

    auto cmd = db::Database::craftGetFestivalTotalsCommand(festivalId);
    auto result = co_await db->runCommand(std::move(cmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        co_return {};
    }

    co_return result.getData<std::vector<db::DBFestivalTeamTotalsData>>();
}

} // namespace nex::rmc
