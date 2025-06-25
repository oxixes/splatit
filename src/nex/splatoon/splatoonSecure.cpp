#include "splatoonSecure.hpp"
#include "../types/common/result.hpp"
#include "../types/splatoonSecure/playingSession.hpp"
#include "../../crypto/tools.hpp"

#include <random>

#include "../types/splatoonSecure/competitionRankingScoreInfo.hpp"

namespace nex::rmc {

SplatoonSecureRMC::SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db) :
                                        Server(std::move(logger)), db(std::move(db)) {
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
}

void SplatoonSecureRMC::requestProbeInitiationExt(ClientInfo client, Request req, List<StationURL> targets, StationURL probe) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    for (auto& target : targets) {
        if (!target.PID.has_value()) {
            res.success = false;
            res.error = Error::CORE__INVALID_ARGUMENT;
            sendMsg(client, res, {});
            return;
        }

        std::unique_lock clientsLock(registeredClientsMutex);

        auto targetClientInfoIt = registeredClients.find(target.PID.value());
        if (targetClientInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            return;
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
        probeParams[0] = std::make_shared<StationURL>(probe);

        sendMsg(targetClientInfo, probeReq, probeParams);
    }

    sendMsg(client, res, {});
}

// FIXME: It seems that if the player fails to connect with another, leaves the gathering without notifying the server
// but it tries to connect two times. To correctly handle removing the player from the gathering, we should check if the
// player has failed to connect with another player and remove it from the gathering if it has, but not on the first
// failed connection.
void SplatoonSecureRMC::reportNatTraversalResult(ClientInfo client, Request req, UInt32 cid, Bool result, UInt32 rtt) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    logger->log(Logger::level::DEBUG, logGroup, "Received NAT traversal result from " + std::to_string(client.pid) + ", success: " + std::to_string(result) + ".");

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::reportNatProperties(ClientInfo client, Request req, UInt32 mapping, UInt32 filtering, UInt32 rtt) {
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
        return;
    }

    clientIt->second.lastReportedNATProperties = {mapping, filtering, rtt};

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::secure_register(ClientInfo client, Request req, List<StationURL> urls) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(3);

    Result retval;
    retval.success = true;
    retval.code = Error::CORE__UNKNOWN; // This means success

    if (urls.size() != 1) {
        retval.success = false;
        retval.code = Error::CORE__INVALID_ARGUMENT;

        params[0] = std::make_shared<Result>(retval);
        params[1] = std::make_shared<UInt32>();
        params[1] = std::make_shared<StationURL>();

        sendMsg(client, res, params);
        return;
    }

    StationURL urlPublic;
    urlPublic.proto = Protocol::PRUDP;
    urlPublic.ip = client.address.address;
    urlPublic.port = client.address.address.port;
    urlPublic.natf = 0;
    urlPublic.natm = 0;
    urlPublic.pmp = 0;
    urlPublic.sid = 15;
    urlPublic.type = 3;
    urlPublic.upnp = 0;

    std::unique_lock rvConnIdLock(rvConnIdMutex);
    params[0] = std::make_shared<Result>(retval);
    params[1] = std::make_shared<UInt32>(0, nextRVConnId);
    params[2] = std::make_shared<StationURL>(urlPublic);

    urls[0].RVCID = nextRVConnId;

    auto clientInfo = SplatoonRegisteredClientInfo();
    clientInfo.client = client;
    clientInfo.urls = std::vector(urls.begin(), urls.end());
    clientInfo.publicUrl = urlPublic;
    clientInfo.rvConnId = nextRVConnId;

    nextRVConnId++;
    if (nextRVConnId == 0) nextRVConnId++; // 0 is not a valid RVConnID
    rvConnIdLock.unlock();

    sendMsg(client, res, params);

    std::unique_lock clientsLock(registeredClientsMutex);
    registeredClients[client.pid] = clientInfo;
}

void SplatoonSecureRMC::replaceUrl(ClientInfo client, Request req, StationURL oldUrl, StationURL newUrl) {
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
        return;
    }

    auto& clientInfo = clientInfoIt->second;

    auto urlIt = std::find(clientInfo.urls.begin(), clientInfo.urls.end(), oldUrl);
    if (urlIt == clientInfo.urls.end()) {
        sendMsg(client, res, {});
        return;
    }

    *urlIt = newUrl;

    sendMsg(client, res, {});
}

// FIXME: In regards to the fixme in reportNATTraversalResult, the report may contain what we are looking for
// to remove the player from the gathering
void SplatoonSecureRMC::sendReport(ClientInfo client, Request req, UInt32 id, qBuffer report) {
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
    for (auto& byte : report.data) {
        // Print the hex value of the byte
        reportStream << std::hex << std::setw(2) << std::setfill('0') << (int) byte;
    }
    std::string reportStr = reportStream.str();
    std::cout << "Report: " << reportStr << std::endl;

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::unregisterGathering(ClientInfo client, Request req, UInt32 gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(1);

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end() || sessionIt->second.session->ownerPid != client.pid) {
        params[0] = std::make_shared<Bool>(0, false);
        sendMsg(client, res, params);
        return;
    }

    unregisterGathering_internal(gId, client.pid);

    params[0] = std::make_shared<Bool>(0, true);
    sendMsg(client, res, params);
}

void SplatoonSecureRMC::findBySingleId(ClientInfo client, Request req, UInt32 id) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(2);

    AnyDataHolder data;

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(id);
    if (sessionIt == matchmakeSessions.end()) {
        params[0] = std::make_shared<Bool>(0, false);

        Gathering gathering(client.minorVersion);
        data.set(gathering, "Gathering");
        params[1] = std::make_shared<AnyDataHolder>(data);

        sendMsg(client, res, params);
        return;
    }

    params[0] = std::make_shared<Bool>(0, true);

    Gathering gathering(client.minorVersion);
    gathering = *sessionIt->second.session;

    data.set(gathering, "Gathering");
    params[1] = std::make_shared<AnyDataHolder>(data);

    sendMsg(client, res, params);
}

void SplatoonSecureRMC::getSessionUrls(ClientInfo client, Request req, UInt32 gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(1);

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, params);
        return;
    }

    auto& sessionInfo = sessionIt->second;

    List<StationURL> urls(client.minorVersion);

    std::unique_lock clientsLock(registeredClientsMutex);

    StationURL internalPublicUrl = registeredClients[sessionInfo.session->hostPid].urls[0];
    StationURL hostPublicUrl = registeredClients[sessionInfo.session->hostPid].publicUrl;
    uint32_t hostRVCID = registeredClients[sessionInfo.session->hostPid].rvConnId;

    internalPublicUrl.RVCID = hostRVCID;
    hostPublicUrl.RVCID = hostRVCID;
    internalPublicUrl.PID = sessionInfo.session->hostPid;
    hostPublicUrl.PID = sessionInfo.session->hostPid;

    urls.push_back(internalPublicUrl);
    urls.push_back(hostPublicUrl);

    params[0] = std::make_shared<List<StationURL>>(urls);

    sendMsg(client, res, params);
}

void SplatoonSecureRMC::updateSessionHost(ClientInfo client, Request req, UInt32 gId, Bool migrateOwner) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        return;
    }

    auto& sessionInfo = sessionIt->second;

    if (!sessionInfo.players.contains(client.pid)) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        return;
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

void SplatoonSecureRMC::endParticipation(ClientInfo client, Request req, UInt32 gId, String msg) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(1);

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end() || !sessionIt->second.players.contains(client.pid)) {
        params[0] = std::make_shared<Bool>(0, false);
        sendMsg(client, res, {});
        return;
    }

    removePlayerFromSession(gId, client.pid, msg);

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::closeParticipation(ClientInfo client, Request req, UInt32 gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        return;
    }

    sessionIt->second.session->openParticipation = false;

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::openParticipation(ClientInfo client, Request req, UInt32 gId) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        return;
    }

    sessionIt->second.session->openParticipation = true;

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::modifyCurrentGameAttribute(ClientInfo client, Request req, UInt32 gId, UInt32 attribIndex, UInt32 newValue) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        return;
    }

    if (attribIndex < (uint32_t) sessionIt->second.session->attributes.size()) {
        logger->log(Logger::level::DEBUG, logGroup, "Modifying attribute " + std::to_string(attribIndex) + " to " + std::to_string(newValue) + " in session " + std::to_string(gId) + " owned by " + std::to_string(client.pid) + ".");
        // sessionIt->second.session->attributes[attribIndex] = newValue;
    }

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::getPlayingSessions(ClientInfo client, Request req, List<PID> pids) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::unique_lock clientsLock(registeredClientsMutex);

    List<PlayingSession> sessions(client.minorVersion);
    for (const auto& pid : std::vector(pids.begin(), pids.end())) {
        auto clientInfoIt = registeredClients.find(pid);
        if (clientInfoIt == registeredClients.end()) continue;
        if (clientInfoIt->second.joinedGathering == nullptr) continue;

        PlayingSession session(client.minorVersion);
        session.pid = pid;
        session.gathering = *clientInfoIt->second.joinedGathering;

        sessions.push_back(std::move(session));
    }

    std::vector<T_ptr> params(1);
    params[0] = std::make_shared<List<PlayingSession>>(sessions);

    sendMsg(client, res, params);
}

void SplatoonSecureRMC::updateProgressScore(ClientInfo client, Request req, UInt32 gId, UInt8 score) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::unique_lock sessionsLock(matchmakeSessionsMutex);

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        return;
    }

    if (sessionIt->second.session->ownerPid != client.pid) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
        sendMsg(client, res, {});
        return;
    }

    sessionIt->second.session->progressScore = score;

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::createMatchmakeSessionWithParam(ClientInfo client, Request req, CreateMatchmakeSessionParam param) {
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
        return;
    }

    auto& clientInfo = clientInfoIt->second;
    if (clientInfo.joinedGathering != nullptr) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__ALREADY_PARTICIPATED_GATHERING; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        return;
    }

    if ((uint32_t) param.srcMatchmakeSession.maxParticipants < param.additionalParticipants.size() + 1) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__SESSION_FULL; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        return;
    }

    logger->log(Logger::level::DEBUG, logGroup, "Creating matchmake session with " + std::to_string(param.additionalParticipants.size()) + " additional participants.");

    MatchmakeSession session = param.srcMatchmakeSession;
    session.id = getNewGatheringId();
    session.ownerPid = client.pid;
    session.hostPid = client.pid;
    session.participationCount = (uint32_t) param.additionalParticipants.size() + 1;
    session.openParticipation = true;
    session.sessionKey = Buffer(crypto::genKey());
    session.startedTime = std::chrono::system_clock::now();

    std::vector<uint32_t> playerPids;
    playerPids.push_back(client.pid);
    for (auto& pid : param.additionalParticipants) {
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            return;
        } else if (playerInfoIt->second.joinedGathering != nullptr) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__ALREADY_PARTICIPATED_GATHERING;
            sendMsg(client, res, {});
            return;
        }

        playerPids.push_back(pid);
    }

    if (!session.userPassword.empty()) session.userPasswordEnabled = true; // Why is this not set by the client?

    logger->log(Logger::level::DEBUG, logGroup, "Matchmake session created with ID " + std::to_string(session.id) + " for " + std::to_string(client.pid) + ":\n" + session.toString());

    // All checks passed, add players to session and send success
    matchmakeSessions[session.id] = {std::make_shared<MatchmakeSession>(session), std::set<uint32_t>(playerPids.begin(), playerPids.end())};

    session.minorVersion = client.minorVersion;
    std::vector<T_ptr> params(1);
    params[0] = std::make_shared<MatchmakeSession>(session);

    sendMsg(client, res, params);

    auto sessionPtr = matchmakeSessions[session.id].session;
    // Send notifications to all players
    // FIXME Actually send only to owner
    for (auto& pid : playerPids) {
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) continue;

        playerInfoIt->second.joinedGathering = sessionPtr;

        for (auto& playerPid : playerPids) {
            sendNotification(playerInfoIt->second.client, NotificationType::NEW_PARTICIPANT,
                             client.pid, session.id, playerPid, param.joinMessage, 1);
        }
    }
}

void SplatoonSecureRMC::joinMatchmakeSessionWithParam(ClientInfo client, Request req, JoinMatchmakeSessionParam param) {
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
        return;
    }

    auto& clientInfo = clientInfoIt->second;
    if (clientInfo.joinedGathering != nullptr) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__ALREADY_PARTICIPATED_GATHERING; // Guess, probably not what the real server sends
        sendMsg(client, res, {});
        return;
    }

    auto sessionIt = matchmakeSessions.find(param.gid);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        return;
    }

    auto& sessionInfo = sessionIt->second;
    if ((uint32_t) sessionInfo.session->participationCount + param.additionalParticipants.size() >
        (uint16_t) sessionInfo.session->maxParticipants) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__SESSION_FULL;
        sendMsg(client, res, {});
        return;
    }

    if (!sessionInfo.session->openParticipation) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__SESSION_CLOSED;
        sendMsg(client, res, {});
        return;
    }

    if (sessionInfo.session->userPasswordEnabled && param.userPassword != sessionInfo.session->userPassword) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__MATCHMAKE_SESSION_USER_PASSWORD_UNMATCH;
        sendMsg(client, res, {});
        return;
    }

    std::vector<uint32_t> playerPids;
    playerPids.push_back(client.pid);
    for (auto& pid : param.additionalParticipants) {
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            return;
        } else if (playerInfoIt->second.joinedGathering != nullptr) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__ALREADY_PARTICIPATED_GATHERING;
            sendMsg(client, res, {});
            return;
        }

        playerPids.push_back(pid);
    }

    // All checks passed, add players to session and send success
    sessionIt->second.session->participationCount += (uint32_t) playerPids.size();

    std::vector<T_ptr> params(1);
    params[0] = sessionIt->second.session;
    params[0]->minorVersion = client.minorVersion;

    sendMsg(client, res, params);

    for (auto& pid : playerPids) {
        auto playerInfoIt = registeredClients.find(pid);
        playerInfoIt->second.joinedGathering = sessionInfo.session;

        for (auto& existingPlayerPid : sessionIt->second.players) {
            auto existingPlayerInfoIt = registeredClients.find(existingPlayerPid);
            if (existingPlayerInfoIt == registeredClients.end()) continue;

            sendNotification(existingPlayerInfoIt->second.client, NotificationType::NEW_PARTICIPANT,
                             client.pid, sessionIt->first, pid, param.joinMessage, 1);
        }
    }

    sessionIt->second.players.insert(playerPids.begin(), playerPids.end());

    for (auto& pid : playerPids) {
        auto playerInfoIt = registeredClients.find(pid);
        if (playerInfoIt == registeredClients.end()) continue;

        for (auto& existingPlayerPid : sessionIt->second.players) {
            sendNotification(playerInfoIt->second.client, NotificationType::NEW_PARTICIPANT,
                             client.pid, sessionIt->first, existingPlayerPid, param.joinMessage, 1);
        }
    }
}

// FIXME: Check if players can be in multiple sessions at the same time
// FIXME: What happens if the session found is where the player is already in?
void SplatoonSecureRMC::autoMatchmakeWithParam_Postpone(ClientInfo client, Request req, AutoMatchmakeParam param) {
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
        return;
    }

    auto& clientInfo = clientInfoIt->second;
    for (auto& additionalPlayer : param.additionalParticipants) {
        auto playerInfoIt = registeredClients.find(additionalPlayer);
        if (playerInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__USER_IS_OFFLINE;
            sendMsg(client, res, {});
            return;
        } else if (playerInfoIt->second.joinedGathering != nullptr &&
                     (clientInfo.joinedGathering == nullptr ||
                     playerInfoIt->second.joinedGathering->id != clientInfo.joinedGathering->id ||
                     playerInfoIt->second.joinedGathering->ownerPid != client.pid)) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__PERMISSION_DENIED;
            sendMsg(client, res, {});
            return;
        }
    }

    logger->log(Logger::level::DEBUG, logGroup, "AutoMatchmakeParam from " + std::to_string(client.pid) + ":\n" + param.toString());

    auto filter = [&](const SessionInfo& sessionInfo) -> bool {
        bool valid = true;

        // Filter out sessions that are not open for participation or don't have enough space
        if (clientInfo.joinedGathering != nullptr && clientInfo.joinedGathering->id == sessionInfo.session->id) valid = false;
        if (!sessionInfo.session->openParticipation) valid = false;
        uint32_t vacantParticipants = param.additionalParticipants.size() + 1;
        uint32_t maxAllowedExistingPlayers = (uint32_t) sessionInfo.session->maxParticipants - vacantParticipants;
        if (sessionInfo.session->participationCount > (uint32_t) maxAllowedExistingPlayers) valid = false;

        if (!valid) return false;

        bool sessionValid = false;
        for (int i = 0; i < param.searchCriteria.size() && !sessionValid; i++) {
            sessionValid = true;
            auto& criteria = param.searchCriteria[i];
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

            auto minParticipants = (std::string) criteria.minParticipants;
            if (minParticipants.find(',') == std::string::npos) {
                throw std::logic_error("minParticipants does not contain a comma");
            }
            uint16_t min_minParticipants = std::stoi(minParticipants.substr(0, minParticipants.find(',')));
            uint16_t max_minParticipants = std::stoi(minParticipants.substr(minParticipants.find(',') + 1));

            auto maxParticipants = (std::string) criteria.maxParticipants;
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
    for (auto& sessionInfo : matchmakeSessions) {
        try {
            if (filter(sessionInfo.second)) {
                uint32_t sessionExp = sessionInfo.second.session->attributes[1];
                uint32_t userExp = param.srcMatchmakeSession.attributes[1];
                validSessions.emplace(abs((int64_t) sessionExp - userExp), sessionInfo.second);
            }
        } catch (const std::logic_error& e) {
            res.success = false;
            res.error = Error::CORE__INVALID_ARGUMENT;

            logger->log(Logger::level::WARN, logGroup,
                        "Malformed AutoMatchmakeParam from " + util::ipv4ToString(client.address.address) + ": " +
                        std::string(e.what()));

            sendMsg(client, res, {});
            return;
        }
    }

    std::vector<T_ptr> params(1);
    SessionInfo sessionInfo;

    if (validSessions.empty()) {
        auto session = std::make_shared<MatchmakeSession>(std::move(param.srcMatchmakeSession));

        session->id = getNewGatheringId();
        session->ownerPid = client.pid;
        session->hostPid = client.pid;
        if (session->gameMode == (uint32_t) 12) { // Festival, I don't know why it is set to not open by default
            session->openParticipation = true;
        }
        session->participationCount = 0;
        session->sessionKey = Buffer(crypto::genKey());
        session->startedTime = std::chrono::system_clock::now();

        logger->log(Logger::level::INFO, logGroup, "Creating new session " + std::to_string(session->id) + " for " + std::to_string(client.pid) + ":\n" + session->toString());

        std::set<uint32_t> players;
        players.insert(client.pid);
        for (auto& pid : param.additionalParticipants) players.insert(pid);

        matchmakeSessions.insert({session->id, {session, players}});
        sessionInfo = matchmakeSessions[session->id];
        params[0] = session;
    } else {
        auto& session = matchmakeSessions[validSessions.top().second.session->id];
        session.players.insert(client.pid);
        for (auto& pid : param.additionalParticipants) session.players.insert(pid);
        sessionInfo = session;
        params[0] = sessionInfo.session;
    }

    sessionInfo.session->participationCount += static_cast<uint32_t>(param.additionalParticipants.size()) + 1;

    params[0]->minorVersion = client.minorVersion;
    sendMsg(client, res, params);

    std::set<uint32_t> newPlayers;
    newPlayers.insert(client.pid);
    for (auto& pid : param.additionalParticipants) newPlayers.insert(pid);

    uint32_t originalGatheringId = 0;
    if (clientInfo.joinedGathering != nullptr) {
        originalGatheringId = clientInfo.joinedGathering->id;
    }

    // Switch the players to the new gathering
    std::set<uint32_t> switchedPlayers;
    for (auto& playerPid : newPlayers) {
        auto playerInfoIt = registeredClients.find(playerPid);
        // We don't need to check if the player is online, because it has been checked before

        if (playerInfoIt->second.joinedGathering != nullptr && playerInfoIt->second.joinedGathering->id != sessionInfo.session->id) {
            switchedPlayers.insert(playerPid);

            matchmakeSessions[originalGatheringId].players.erase(playerPid);
            sendNotification(playerInfoIt->second.client, NotificationType::SWITCH_GATHERING,
                             client.pid, sessionInfo.session->id, playerPid, "", 1);
        }

        playerInfoIt->second.joinedGathering = sessionInfo.session;
    }

    // If the original gathering were to still have players, we would need to send notifications to them
    if (originalGatheringId != 0) {
        for (auto& pid : switchedPlayers) {
            removePlayerFromSession(originalGatheringId, pid, "", false, true);
        }
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

void SplatoonSecureRMC::getCompetitionRankingScore(ClientInfo client, Request req, CompetitionRankingGetParam param)
{
    // TODO We don't know yet what this should return, so for debugging purposes, we'll just return a dummy value.
    logger->log(Logger::level::DEBUG, logGroup, "getCompetitionRankingScore called with param: " + param.toString());

    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    std::vector<T_ptr> params(1);

    List<CompetitionRankingScoreInfo> scores(client.minorVersion);
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
    data.unk3 = 0xCAFE0007;
    Datetime now(client.minorVersion);
    data.uploadDate = now;
    data.unk4 = true;
    qBuffer buffer;
    std::vector<uint8_t> dummyData = {0xCA, 0xFE, 0x00, 0x09};
    buffer.data = std::move(dummyData);
    data.metadata = std::move(buffer);

    //scoreData.push_back(data);
    scoreInfo.scoreData = std::move(scoreData);

    scores.push_back(scoreInfo);

    params[0] = std::make_shared<List<CompetitionRankingScoreInfo>>(scores);

    logger->log(Logger::level::DEBUG, logGroup, "Returning dummy competition ranking score for client " + std::to_string(client.pid) + ": " + scores.toString());

    sendMsg(client, res, params);
}

void SplatoonSecureRMC::onDisconnect(prudp::PRUDPAddress address) {
    std::unique_lock sessionsMutex(matchmakeSessionsMutex);
    std::unique_lock clientLock(registeredClientsMutex);
    std::unique_lock pidMapLock(pidMapMutex);
    auto clientIt = registeredClients.find(pidMap[address]);
    if (clientIt != registeredClients.end() && clientIt->second.joinedGathering != nullptr) {
        removePlayerFromSession(clientIt->second.joinedGathering->id, pidMap[address], "", true);
    }

    if (clientIt != registeredClients.end()) registeredClients.erase(pidMap[address]);

    clientLock.unlock();
    Server::onDisconnect(address);
}

uint32_t SplatoonSecureRMC::getNewGatheringId() {
    // We choose a random number between 1000 and UINT32_MAX, and if it's already taken, we try again.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(1000, UINT32_MAX);

    uint32_t id = dis(gen);

    std::unique_lock sessionsLock(matchmakeSessionsMutex);
    while (matchmakeSessions.find(id) != matchmakeSessions.end()) {
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
    NotificationEvent event(client.minorVersion);
    event.type = type;
    event.srcPid = srcPid;
    event.param1 = param1;
    event.param2 = param2;
    event.strParam = strParam;
    event.param3 = param3;

    params[0] = std::make_shared<NotificationEvent>(event);

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
        clientInfo.joinedGathering = nullptr;
    }

    matchmakeSessions.erase(sessionIt);
}

void SplatoonSecureRMC::removePlayerFromSession(uint32_t gId, uint32_t playerPid, const std::string& msg, bool disconnected, bool switching) {
    std::unique_lock sessionsMutex(matchmakeSessionsMutex);
    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) return;

    std::unique_lock clientsMutex(registeredClientsMutex);
    if (!disconnected && !switching) sendNotification(registeredClients[playerPid].client, NotificationType::PARTICIPATION_ENDED,
                                        playerPid, sessionIt->second.session->id, playerPid, msg, 0);

    sessionIt->second.players.erase(playerPid);

    if (!switching) registeredClients[playerPid].joinedGathering = nullptr;
    if (sessionIt->second.players.empty()) {
        unregisterGathering_internal(gId, playerPid);
        return;
    } else {
        uint32_t ownerPid = sessionIt->second.session->ownerPid;
        sendNotification(registeredClients[ownerPid].client, (disconnected) ? NotificationType::PARTICIPANT_DISCONNECTED : NotificationType::PARTICIPATION_ENDED,
                         playerPid, sessionIt->second.session->id, playerPid, msg, 0);
        // for (auto& pid : sessionIt->second.players) {
        //     sendNotification(registeredClients[pid].client,
        //                      (disconnected) ? NotificationType::PARTICIPANT_DISCONNECTED : NotificationType::PARTICIPATION_ENDED,
        //                      playerPid, sessionIt->second.session->id, playerPid, msg, 0);
        // }
    }

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