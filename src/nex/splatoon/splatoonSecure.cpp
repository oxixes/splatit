#include "splatoonSecure.hpp"
#include "../types/common/result.hpp"
#include "../types/splatoonSecure/playingSession.hpp"
#include "../../crypto/tools.hpp"

#include <random>

#include "../types/splatoonSecure/competitionRankingScoreInfo.hpp"

namespace nex::rmc {

using namespace async;

SplatoonSecureRMC::SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                                     std::shared_ptr<ss::SharedState> sharedState, uint32_t serverId):
                                        Server(std::move(logger), serverId), db(std::move(db)), sharedState(std::move(sharedState)) {
    logGroup = Logger::group::SPLATOON_SECURE;

    // Protocol 3 - NAT Traversal
    REGISTER_CALL(SplatoonSecureRMC::requestProbeInitiationExt, 3, 3);
    REGISTER_CALL(SplatoonSecureRMC::reportNatTraversalResult, 3, 4);
    REGISTER_CALL(SplatoonSecureRMC::reportNatProperties, 3, 5);

    // Protocol 11 - Secure connection
    REGISTER_CALL(SplatoonSecureRMC::secure_register, 11, 1);
    REGISTER_CALL(SplatoonSecureRMC::replaceUrl, 11, 7);
    REGISTER_CALL(SplatoonSecureRMC::sendReport, 11, 8);

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

        std::unique_lock clientsLock(registeredClientsMutex);

        auto targetClientInfoIt = registeredClients.find(target.PID.value());
        if (targetClientInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        ClientInfo targetClientInfo = targetClientInfoIt->second.client;

        Request probeReq;
        probeReq.protocolId = 3;
        probeReq.methodId = 2; // InitiateProbe
        probeReq.extendedProtocolId = 0;
        std::unique_lock reqCallIdLock(reqCallIdMutex);
        probeReq.callId = nextReqCallId++;
        reqCallIdLock.unlock();

        std::vector<T_ptr> probeParams(1);
        probeParams[0] = std::move(probe);

        sendMsg(targetClientInfo, probeReq, probeParams);
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

    std::unique_lock clientsLock(registeredClientsMutex);

    auto clientIt = registeredClients.find(client.pid);
    if (clientIt == registeredClients.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    clientIt->second.lastReportedNATProperties = {*mapping, *filtering, *rtt};

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

    sendMsg(client, res, params);

    std::unique_lock clientsLock(registeredClientsMutex);
    registeredClients[client.pid] = std::move(clientInfo);
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

    std::unique_lock clientsLock(registeredClientsMutex);

    auto clientInfoIt = registeredClients.find(client.pid);
    if (clientInfoIt == registeredClients.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    auto& clientInfo = clientInfoIt->second;

    const auto urlIt = std::ranges::find(clientInfo.urls, *oldUrl);
    if (urlIt == clientInfo.urls.end()) {
        sendMsg(client, res, {});
        co_return;
    }

    *urlIt = *newUrl;

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

    // Log the report
    std::stringstream reportStream;
    for (auto& byte : report->data) {
        // Print the hex value of the byte
        reportStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::string reportStr = reportStream.str();
    std::cout << "Report: " << reportStr << std::endl;

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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end() || sessionIt->second.session->ownerPid != client.pid) {
        params[0] = std::make_unique<Bool>(0, false);
        sendMsg(client, res, params);
        co_return;
    }

    unregisterGathering_internal(*gId, client.pid);

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

    std::unique_ptr<AnyDataHolder> data;

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*id);
    if (sessionIt == matchmakeSessions.end()) {
        params[0] = std::make_unique<Bool>(0, false);

        Gathering gathering(client.minorVersion);
        data->set(gathering, "Gathering");
        params[1] = std::move(data);

        sendMsg(client, res, params);
        co_return;
    }

    params[0] = std::make_unique<Bool>(0, true);

    Gathering gathering(client.minorVersion);
    gathering = static_cast<Gathering>(*sessionIt->second.session);

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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, params);
        co_return;
    }

    auto& sessionInfo = sessionIt->second;

    std::unique_ptr<List<StationURL>> urls = std::make_unique<List<StationURL>>(client.minorVersion);

    std::unique_lock clientsLock(registeredClientsMutex);

    StationURL internalPublicUrl = registeredClients[sessionInfo.session->hostPid].urls[0];
    StationURL hostPublicUrl = registeredClients[sessionInfo.session->hostPid].publicUrl;
    uint32_t hostRVCID = registeredClients[sessionInfo.session->hostPid].rvConnId;

    internalPublicUrl.RVCID = hostRVCID;
    hostPublicUrl.RVCID = hostRVCID;
    internalPublicUrl.PID = sessionInfo.session->hostPid;
    hostPublicUrl.PID = sessionInfo.session->hostPid;

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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end()) {
        logger->log(Logger::level::DEBUG, logGroup, "Update session host failed: Invalid GID " + std::to_string(*gId) + " for client " + std::to_string(client.pid) + ".");
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    auto& sessionInfo = sessionIt->second;

    if (!sessionInfo.players.contains(client.pid)) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionInfo.session->hostPid = client.pid;
    if (migrateOwner) sessionInfo.session->ownerPid = client.pid;

    sendMsg(client, res, {});

    for (auto& pid : sessionInfo.players) {
        std::unique_lock clientsLock(registeredClientsMutex);
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) continue;

        sendNotification(playerInfoIt->second.client, NotificationType::HOST_CHANGED,
                         client.pid, sessionInfo.session->id, client.pid, "", 0);

        if (migrateOwner) {
            uint64_t msNow = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            sendNotification(playerInfoIt->second.client, NotificationType::OWNERSHIP_CHANGED,
                             client.pid, sessionInfo.session->id, client.pid,
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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end()) {
        logger->log(Logger::level::DEBUG, logGroup, "Update session host failed: Invalid GID " + std::to_string(*gId) + " for client " + std::to_string(client.pid) + ".");
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    auto& sessionInfo = sessionIt->second;

    if (!sessionInfo.players.contains(client.pid)) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    std::set<uint32_t> consideredOwners;

    for (const auto& pid: *potentialNewOwners) {
        if (!participantsOnly || sessionInfo.players.contains(pid)) {
            consideredOwners.insert(pid);
        }
    }

    // Also add the session players
    for (const auto& pid : sessionInfo.players) {
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
    sessionInfo.session->ownerPid = newOwnerPid;

    for (const auto& pid : sessionInfo.players) {
        std::unique_lock clientsLock(registeredClientsMutex);
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) continue;

        sendNotification(playerInfoIt->second.client, NotificationType::OWNERSHIP_CHANGED,
                         client.pid, sessionInfo.session->id, newOwnerPid, "", 0);
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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end() || !sessionIt->second.players.contains(client.pid)) {
        params[0] = std::make_unique<Bool>(0, false);
        sendMsg(client, res, {});
        co_return;
    }

    removePlayerFromSession(*gId, client.pid, *msg);

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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionIt->second.session->openParticipation = false;

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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionIt->second.session->openParticipation = true;

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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    if (*attribIndex < static_cast<uint32_t>(sessionIt->second.session->attributes.size())) {
        logger->log(Logger::level::DEBUG, logGroup, "Modifying attribute " + std::to_string(*attribIndex) + " to " + std::to_string(*newValue) + " in session " + std::to_string(*gId) + " owned by " + std::to_string(client.pid) + ".");
        // sessionIt->second.session->attributes[attribIndex] = newValue;
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

    std::unique_lock clientsLock(registeredClientsMutex);

    std::unique_ptr<List<PlayingSession>> sessions = std::make_unique<List<PlayingSession>>(client.minorVersion);
    for (const auto& pid : *pids) {
        auto clientInfoIt = registeredClients.find(pid);
        if (clientInfoIt == registeredClients.end()) continue;

        for (const auto& existingSession : clientInfoIt->second.joinedGatherings) {
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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(*gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        co_return;
    }

    sessionIt->second.session->progressScore = *score;

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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);
    std::unique_lock clientsLock(registeredClientsMutex);

    auto clientInfoIt = registeredClients.find(client.pid);
    if (clientInfoIt == registeredClients.end()) {
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

    std::shared_ptr<MatchmakeSession> session = std::make_shared<MatchmakeSession>(param->srcMatchmakeSession);
    session->id = getNewGatheringId();
    session->ownerPid = client.pid;
    session->hostPid = client.pid;
    session->participationCount = static_cast<uint32_t>(param->additionalParticipants.size()) + 1;
    session->openParticipation = true;
    session->sessionKey = Buffer(crypto::genKey());
    session->startedTime = std::chrono::system_clock::now();

    std::vector<uint32_t> playerPids;
    playerPids.push_back(client.pid);
    for (auto& pid : param->additionalParticipants) {
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        playerPids.push_back(pid);
    }

    if (!session->userPassword.empty()) session->userPasswordEnabled = true; // Why is this not set by the client?

    logger->log(Logger::level::DEBUG, logGroup, "Matchmake session created with ID " + std::to_string(session->id) + " for " + std::to_string(client.pid) + ":\n" + session->toString());

    // All checks passed, add players to session and send success
    matchmakeSessions[session->id] = {session, std::set<uint32_t>(playerPids.begin(), playerPids.end())};

    session->minorVersion = client.minorVersion;
    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<MatchmakeSession>(*session);

    sendMsg(client, res, params);

    auto sessionPtr = matchmakeSessions[session->id].session;
    // Send notifications to all players
    // FIXME Actually send only to owner
    for (auto& pid : playerPids) {
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) continue;

        playerInfoIt->second.joinedGatherings.push_back(sessionPtr);

        for (auto& playerPid : playerPids) {
            sendNotification(playerInfoIt->second.client, NotificationType::NEW_PARTICIPANT,
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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);
    std::unique_lock clientsLock(registeredClientsMutex);
    auto clientInfoIt = registeredClients.find(client.pid);
    if (clientInfoIt == registeredClients.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED;
        sendMsg(client, res, {});
        co_return;
    }

    auto sessionIt = matchmakeSessions.find(param->gid);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        co_return;
    }

    auto&[session, players] = sessionIt->second;
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
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        playerPids.push_back(pid);
    }

    // All checks passed, add players to session and send success
    sessionIt->second.session->participationCount += static_cast<uint32_t>(playerPids.size());

    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<MatchmakeSession>(*sessionIt->second.session);
    params[0]->minorVersion = client.minorVersion;

    sendMsg(client, res, params);

    for (auto& pid : playerPids) {
        auto playerInfoIt = registeredClients.find(pid);
        playerInfoIt->second.joinedGatherings.push_back(session);

        for (auto& existingPlayerPid : sessionIt->second.players) {
            auto existingPlayerInfoIt = registeredClients.find(existingPlayerPid);
            if (existingPlayerInfoIt == registeredClients.end()) continue;

            sendNotification(existingPlayerInfoIt->second.client, NotificationType::NEW_PARTICIPANT,
                             client.pid, sessionIt->first, pid, param->joinMessage, 1);
        }
    }

    sessionIt->second.players.insert(playerPids.begin(), playerPids.end());

    for (auto& pid : playerPids) {
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) continue;

        for (auto& existingPlayerPid : sessionIt->second.players) {
            sendNotification(playerInfoIt->second.client, NotificationType::NEW_PARTICIPANT,
                             client.pid, sessionIt->first, existingPlayerPid, param->joinMessage, 1);
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

    std::unique_lock sessionsLock(matchmakeSessionsMutex);
    std::unique_lock clientsLock(registeredClientsMutex);

    auto clientInfoIt = registeredClients.find(client.pid);
    if (clientInfoIt == registeredClients.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        co_return;
    }

    if (param->gidForParitipationCheck != UInt32(0)) {
        // Check that the client is already in the gathering
        auto sessionIt = matchmakeSessions.find(param->gidForParitipationCheck);
        if (sessionIt == matchmakeSessions.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__INVALID_GID;
            sendMsg(client, res, {});
            co_return;
        }

        auto& sessionInfo = sessionIt->second;
        if (!sessionInfo.players.contains(client.pid)) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
            sendMsg(client, res, {});
            co_return;
        }
    }

    auto& clientInfo = clientInfoIt->second;
    for (auto& additionalPlayer : param->additionalParticipants) {
        auto playerInfoIt = registeredClients.find(additionalPlayer);
        if (playerInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            co_return;
        }

        bool foundGathering = false;

        for (auto& clientGathering : clientInfo.joinedGatherings) {
            for (auto& playerGathering : playerInfoIt->second.joinedGatherings) {
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

    // Log all the sessions that are currently available for matchmaking
    logger->log(Logger::level::DEBUG, logGroup, "Current matchmake sessions:");
    for (const auto &[session, players]: matchmakeSessions | std::views::values) {
        logger->log(Logger::level::DEBUG, logGroup, session->toString());
    }

    auto filter = [&](const SessionInfo& sessionInfo) -> bool {
        bool valid = true;

        // Filter out sessions that are already joined by the client
        for (auto& clientGathering : clientInfo.joinedGatherings) {
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
    for (auto &val: matchmakeSessions | std::views::values) {
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

        session->id = getNewGatheringId();
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

        matchmakeSessions.insert({session->id, {session, players}});
        sessionInfo = matchmakeSessions[session->id];
        params[0] = std::make_unique<MatchmakeSession>(*session);

        logger->log(Logger::level::INFO, logGroup, "Creating new session " + std::to_string(session->id) + " for " + std::to_string(client.pid) + ":\n" + session->toString());
    } else {
        auto& session = matchmakeSessions[validSessions.top().second.session->id];
        session.players.insert(client.pid);
        for (auto& pid : param->additionalParticipants) session.players.insert(pid);
        sessionInfo = session;
        params[0] = std::make_unique<MatchmakeSession>(*sessionInfo.session);

        logger->log(Logger::level::INFO, logGroup, "Joining existing session " + std::to_string(sessionInfo.session->id) + " for " + std::to_string(client.pid) + ":\n" + sessionInfo.session->toString());
    }

    sessionInfo.session->participationCount += static_cast<uint32_t>(param->additionalParticipants.size()) + 1;

    params[0]->minorVersion = client.minorVersion;
    sendMsg(client, res, params);

    std::set<uint32_t> newPlayers;
    newPlayers.insert(client.pid);
    for (auto& pid : param->additionalParticipants) newPlayers.insert(pid);

    // Switch the players to the new gathering
    std::set<uint32_t> switchedPlayers;
    for (auto& playerPid : newPlayers) {
        auto playerInfoIt = registeredClients.find(playerPid);
        // We don't need to check if the player is online, because it has been checked before

        if (param->gidForParitipationCheck != UInt32(0)) {
            switchedPlayers.insert(playerPid);

            sendNotification(playerInfoIt->second.client, NotificationType::SWITCH_GATHERING,
                             client.pid, sessionInfo.session->id, playerPid, "", 1);
        }

        playerInfoIt->second.joinedGatherings.push_back(sessionInfo.session);
    }

    // We notify other players of the new participant(s)
    for (auto& pid : sessionInfo.players) {
//        if (newPlayers.contains(pid)) continue;
        if (sessionInfo.session->ownerPid != pid && client.pid != pid) continue;
        sendNotification(registeredClients[pid].client, NotificationType::NEW_PARTICIPANT, client.pid,
                         sessionInfo.session->id, client.pid, "", 1);
    }

    // And we also send notifications to the new participant(s), one for each player already in the session (including themselves)
//    for (auto& pid : sessionInfo.players) {
//        for (auto& newPlayerPid : newPlayers) {
//            sendNotification(registeredClients[newPlayerPid].client, NotificationType::NEW_PARTICIPANT, client.pid,
//                             sessionInfo.session->id, pid, "", 1);
//        }
//    }
}

Task<void> SplatoonSecureRMC::getCompetitionRankingScore(ClientInfo client, Request req,
                                                         std::unique_ptr<CompetitionRankingGetParam> param) {
    // TODO We don't know yet what this should return, so for debugging purposes, we'll just return a dummy value.
    logger->log(Logger::level::DEBUG, logGroup, "getCompetitionRankingScore called with param: " + param->toString());

    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::vector<T_ptr> params(1);

    std::unique_ptr<List<CompetitionRankingScoreInfo>> scores = std::make_unique<List<CompetitionRankingScoreInfo>>(client.minorVersion);
    CompetitionRankingScoreInfo scoreInfo(client.minorVersion);
    scoreInfo.festivalId = 0x1CE0; // Festival ID
    scoreInfo.unk1 = 0xCAFE0002;

    List<UInt32> unk_vec_1(client.minorVersion);
    unk_vec_1.emplace_back(client.minorVersion, 10);
    unk_vec_1.emplace_back(client.minorVersion, 90);

    List<UInt32> unk_vec_2(client.minorVersion);
    unk_vec_2.emplace_back(client.minorVersion, 40);
    unk_vec_2.emplace_back(client.minorVersion, 60);

    scoreInfo.teamWins = std::move(unk_vec_1);
    scoreInfo.teamVotes = std::move(unk_vec_2);

    List<CompetitionRankingScoreData> scoreData(client.minorVersion);
    CompetitionRankingScoreData data(client.minorVersion);
    data.unk1 = 0xCAFE0005;
    data.userId = 0xCAFE0006;
    data.score = 0xCAFE0007;
    Datetime now(client.minorVersion);
    data.uploadDate = now;
    data.unk4 = true;
    qBuffer buffer;
    std::vector<uint8_t> dummyData = {0xCA, 0xFE, 0x00, 0x09};
    buffer.data = std::move(dummyData);
    data.appData = std::move(buffer);

    //scoreData.push_back(data);
    scoreInfo.scoreData = std::move(scoreData);

    scores->push_back(std::move(scoreInfo));

    params[0] = std::move(scores);

    logger->log(Logger::level::DEBUG, logGroup, "Returning dummy competition ranking score for client " + std::to_string(client.pid) + ": " + scores->toString());

    sendMsg(client, res, params);
    co_return;
}

Task<void> SplatoonSecureRMC::uploadCompetitionRankingScore(ClientInfo client, Request req,
                                                            std::unique_ptr<CompetitionRankingUploadScoreParam> param) {
    logger->log(Logger::level::DEBUG, logGroup, "UploadCompetitionRankingScore called with param: " + param->toString());

    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::vector<T_ptr> params(1);
    params[0] = std::make_unique<Bool>(client.minorVersion, true);

    sendMsg(client, res, params);
    co_return;
}

Task<void> SplatoonSecureRMC::onDisconnect(prudp::PRUDPAddress address) {
    std::unique_lock sessionsMutex(matchmakeSessionsMutex);
    std::unique_lock clientLock(registeredClientsMutex);
    std::unique_lock pidMapLock(pidMapMutex);
    auto clientIt = registeredClients.find(pidMap[address]);
    if (clientIt != registeredClients.end()) {
        for (auto& gathering : clientIt->second.joinedGatherings) {
            // If the client is in a gathering, we remove them from it
            removePlayerFromSession(gathering->id, clientIt->second.client.pid, "", true);
        }
    }

    if (clientIt != registeredClients.end()) registeredClients.erase(pidMap[address]);

    clientLock.unlock();
    Server::onDisconnect(address);
    co_return;
}

uint32_t SplatoonSecureRMC::getNewGatheringId() {
    // We choose a random number between 1000 and UINT32_MAX, and if it's already taken, we try again.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(1000, UINT32_MAX);

    uint32_t id = dis(gen);

    std::unique_lock sessionsLock(matchmakeSessionsMutex);
    while (matchmakeSessions.contains(id)) {
        id = dis(gen);
    }

    return id;
}

void SplatoonSecureRMC::sendNotification(ClientInfo client, NotificationType type, uint32_t srcPid, uint32_t param1,
                                         uint32_t param2, const std::string& strParam, uint32_t param3) {
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
}

void SplatoonSecureRMC::unregisterGathering_internal(uint32_t gId, uint32_t srcPid) {
    std::unique_lock sessionsMutex(matchmakeSessionsMutex);
    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) return;

    std::unique_lock clientsMutex(registeredClientsMutex);
    auto& sessionInfo = sessionIt->second;
    for (auto& pid : sessionInfo.players) {
        auto clientIt = registeredClients.find(pid);

        sendNotification(clientIt->second.client, NotificationType::GATHERING_UNREGISTERED, srcPid, gId, 0, "", 0);

        auto& clientInfo = clientIt->second;
        
        // Remove the gathering from the client's joinedGatherings
        auto gatheringIt = std::ranges::find_if(clientInfo.joinedGatherings,
                                                [gId](const std::shared_ptr<Gathering>& gathering) {
                                                    return gathering->id == gId;
                                                });

        if (gatheringIt != clientInfo.joinedGatherings.end()) {
            clientInfo.joinedGatherings.erase(gatheringIt);
        }
    }

    matchmakeSessions.erase(sessionIt);
}

void SplatoonSecureRMC::removePlayerFromSession(uint32_t gId, uint32_t playerPid, const std::string& msg, bool disconnected) {
    std::unique_lock sessionsMutex(matchmakeSessionsMutex);
    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) return;

    std::unique_lock clientsMutex(registeredClientsMutex);

    if (!sessionIt->second.players.contains(playerPid)) {
        logger->log(Logger::level::WARN, logGroup, "Player " + std::to_string(playerPid) + " tried to leave session " + std::to_string(gId) + ", but is not in it.");
        return;
    }

    auto clientInfoIt = registeredClients.find(playerPid);
    if (clientInfoIt == registeredClients.end()) {
        logger->log(Logger::level::WARN, logGroup, "Player " + std::to_string(playerPid) + " tried to leave session " + std::to_string(gId) + ", but is not registered.");
        return;
    }

    auto& clientInfo = registeredClients[playerPid];

    if (!disconnected) sendNotification(registeredClients[playerPid].client, NotificationType::PARTICIPATION_ENDED,
                                        playerPid, sessionIt->second.session->id, playerPid, msg, 0);

    sessionIt->second.players.erase(playerPid);
    sessionIt->second.session->participationCount--;

    // Remove the gathering from the client's joinedGatherings
    const auto gatheringIt = std::ranges::find_if(clientInfo.joinedGatherings,
                                            [gId](const std::shared_ptr<Gathering>& gathering) {
                                                return gathering->id == gId;
                                            });

    if (gatheringIt != clientInfo.joinedGatherings.end()) {
        clientInfo.joinedGatherings.erase(gatheringIt);
    }

    if (sessionIt->second.players.empty()) {
        unregisterGathering_internal(gId, playerPid);
        return;
    }

    uint32_t ownerPid = sessionIt->second.session->ownerPid;
    sendNotification(registeredClients[ownerPid].client, disconnected ? NotificationType::PARTICIPANT_DISCONNECTED : NotificationType::PARTICIPATION_ENDED,
                     playerPid, sessionIt->second.session->id, playerPid, msg, 0);
    // for (auto& pid : sessionIt->second.players) {
    //     sendNotification(registeredClients[pid].client,
    //                      (disconnected) ? NotificationType::PARTICIPANT_DISCONNECTED : NotificationType::PARTICIPATION_ENDED,
    //                      playerPid, sessionIt->second.session->id, playerPid, msg, 0);
    // }

    if (sessionIt->second.session->ownerPid == playerPid) {
        // TODO Choose the best player as the new owner
        sessionIt->second.session->ownerPid = *sessionIt->second.players.begin();
        for (auto& pid : sessionIt->second.players) {
            uint64_t msNow = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            sendNotification(registeredClients[pid].client, NotificationType::OWNERSHIP_CHANGED, playerPid,
                             sessionIt->second.session->id, sessionIt->second.session->ownerPid,
                             std::to_string(msNow), 0);
        }
    }

    if (sessionIt->second.session->hostPid == playerPid) {
        // TODO Choose the best player as the new host
        sessionIt->second.session->hostPid = *sessionIt->second.players.begin();
        for (auto& pid : sessionIt->second.players) {
            sendNotification(registeredClients[pid].client, NotificationType::HOST_CHANGED, playerPid,
                             sessionIt->second.session->id, sessionIt->second.session->hostPid, "", 0);
        }
    }
}

} // namespace nex::rmc