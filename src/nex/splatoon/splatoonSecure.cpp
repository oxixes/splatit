#include "splatoonSecure.hpp"
#include "../types/common/result.hpp"
#include "../types/splatoonSecure/playingSession.hpp"
#include "../../crypto/tools.hpp"

#include <random>

// TODO Add mutex
// TODO Change minor version of returned objects to the client's minor version

namespace nex::rmc {

SplatoonSecureRMC::SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db) :
                                        Server(std::move(logger)), db(std::move(db)) {
    logGroup = Logger::group::SPLATOON_SECURE;

    // Protocol 3 - NAT Traversal
    registerCall(this, &SplatoonSecureRMC::requestProbeInitiationExt, 3, 3);
    registerCall(this, &SplatoonSecureRMC::reportNatTraversalResult, 3, 4);
    registerCall(this, &SplatoonSecureRMC::reportNatProperties, 3, 5);

    // Protocol 11 - Secure connection
    registerCall(this, &SplatoonSecureRMC::secure_register, 11, 1);
    registerCall(this, &SplatoonSecureRMC::replaceUrl, 11, 7);
    registerCall(this, &SplatoonSecureRMC::sendReport, 11, 8);

    // Protocol 21 - Matchmaking
    registerCall(this, &SplatoonSecureRMC::unregisterGathering, 21, 2);
    registerCall(this, &SplatoonSecureRMC::findBySingleId, 21, 21);
    registerCall(this, &SplatoonSecureRMC::getSessionUrls, 21, 41);

    // Protocol 50 - Matchmaking (Extension)
    registerCall(this, &SplatoonSecureRMC::endParticipation, 50, 1);

    // Protocol 109 - Matchmake Extension
    registerCall(this, &SplatoonSecureRMC::closeParticipation, 109, 1);
    registerCall(this, &SplatoonSecureRMC::getPlayingSessions, 109, 16);
    registerCall(this, &SplatoonSecureRMC::updateProgressScore, 109, 34);
    registerCall(this, &SplatoonSecureRMC::autoMatchmakeWithParam_Postpone, 109, 40);
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

        auto targetClientInfoIt = registeredClients.find(target.PID.value());
        if (targetClientInfoIt == registeredClients.end()) {
            res.success = false;
            res.error = Error::RENDEZ_VOUS__INVALID_PID;
            sendMsg(client, res, {});
            return;
        }

        ClientInfo targetClientInfo = targetClientInfoIt->second.client;

        Request probeReq;
        probeReq.protocolId = 3;
        probeReq.methodId = 2; // InitiateProbe
        probeReq.extendedProtocolId = 0;
        probeReq.callId = nextReqCallId++;

        std::vector<T_ptr> probeParams(1);
        probeParams[0] = std::make_shared<StationURL>(probe);

        sendMsg(targetClientInfo, probeReq, probeParams);
    }

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::reportNatTraversalResult(ClientInfo client, Request req, UInt32 cid, Bool result, UInt32 rtt) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::reportNatProperties(ClientInfo client, Request req, UInt32 mapping, UInt32 filtering, UInt32 rtt) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

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

    params[0] = std::make_shared<Result>(retval);
    params[1] = std::make_shared<UInt32>(0, nextRVConnId);
    params[2] = std::make_shared<StationURL>(urlPublic);

    urls[0].RVCID = nextRVConnId;

    auto clientInfo = RegisteredClientInfo();
    clientInfo.client = client;
    clientInfo.urls = (std::vector<StationURL>) std::move(urls);
    clientInfo.publicUrl = urlPublic;
    clientInfo.rvConnId = nextRVConnId;

    nextRVConnId++;
    if (nextRVConnId == 0) nextRVConnId++; // 0 is not a valid RVConnID

    registeredClients[client.pid] = clientInfo;

    sendMsg(client, res, params);
}

void SplatoonSecureRMC::replaceUrl(ClientInfo client, Request req, StationURL oldUrl, StationURL newUrl) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

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

void SplatoonSecureRMC::sendReport(ClientInfo client, Request req, UInt32 id, qBuffer report) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    // We'll just ignore the report for now. It has a header and a payload that is zlib compressed and then encrypted
    // with AES-ECB, with key 901edf193dc5ef3c5290647bff20c385.

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

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, params);
        return;
    }

    auto& sessionInfo = sessionIt->second;

    List<StationURL> urls(client.minorVersion);

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

void SplatoonSecureRMC::endParticipation(ClientInfo client, Request req, UInt32 gId, String msg) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;
    std::vector<T_ptr> params(1);

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

    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__INVALID_GID;
        sendMsg(client, res, {});
        return;
    }

    if (!sessionIt->second.players.contains(client.pid)) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_PARTICIPATED_GATHERING;
        sendMsg(client, res, {});
        return;
    }

    removePlayerFromSession(gId, client.pid);

    sendMsg(client, res, {});
}

void SplatoonSecureRMC::getPlayingSessions(ClientInfo client, Request req, List<PID> pids) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    List<PlayingSession> sessions(client.minorVersion);
    for (const auto& pid : (std::vector<PID>) pids) {
        auto clientInfoIt = registeredClients.find(pid);
        if (clientInfoIt == registeredClients.end()) continue;
        if (clientInfoIt->second.joinedGathering == nullptr) continue;

        PlayingSession session(client.minorVersion);
        session.pid = pid;
        session.gathering = *clientInfoIt->second.joinedGathering;

        ((std::vector<PlayingSession>) sessions).push_back(std::move(session));
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

void SplatoonSecureRMC::autoMatchmakeWithParam_Postpone(ClientInfo client, Request req, AutoMatchmakeParam param) {
    Response res;
    res.protocolId = req.protocolId;
    res.methodId = req.methodId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.callId = req.callId;
    res.success = true;

    auto clientInfoIt = registeredClients.find(client.pid);
    if (clientInfoIt == registeredClients.end()) {
        res.success = false;
        res.error = Error::RENDEZ_VOUS__NOT_AUTHENTICATED; // Guess, probably not what the real server sends
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

    auto filter = [&](const SessionInfo& sessionInfo) -> bool {
        bool valid = true;
        for (int i = 0; i < param.searchCriteria.size() && valid; i++) {
            auto& criteria = param.searchCriteria[i];
            if (criteria.attributes.size() != sessionInfo.session->attributes.size()) {
                valid = false;
                continue;
            }

            for (int j = 0; j < criteria.attributes.size() && valid; j++) {
                auto& attribute = criteria.attributes[j];
                uint32_t attrValue = std::stoi(attribute);
                if (j == 1) continue; // j == 1 is the player exp (not the one shown in game), so it can differ.
                if (attrValue != sessionInfo.session->attributes[j]) valid = false;
            }

            if (std::stoi(criteria.gameMode) != sessionInfo.session->gameMode) valid = false;
            if (std::stoi(criteria.minParticipants) != sessionInfo.session->minParticipants) valid = false;
            if (std::stoi(criteria.maxParticipants) != sessionInfo.session->maxParticipants) valid = false;
            if (std::stoi(criteria.matchmakeSystemType) != sessionInfo.session->matchmakeSystemType) valid = false;
            uint16_t vacantParticipants = criteria.vacantParticipants;
            if (vacantParticipants == 0) vacantParticipants = 1;
            uint16_t maxAllowedExistingPlayers = sessionInfo.session->maxParticipants - vacantParticipants;
            if (sessionInfo.session->participationCount > (uint32_t) maxAllowedExistingPlayers) valid = false;
            if (criteria.excludeLocked && !sessionInfo.session->openParticipation) valid = false;
            if (criteria.excludeUserPasswordSet && sessionInfo.session->userPasswordEnabled) valid = false;
            if (criteria.excludeSystemPasswordSet && sessionInfo.session->systemPasswordEnabled) valid = false;
            if (criteria.referGid != sessionInfo.session->referGid) valid = false;

            for (auto& mmParam : criteria.matchmakeParam.params) {
                auto sessionIt = sessionInfo.session->matchmakeParam.params.find(mmParam.first);
                if (sessionIt == sessionInfo.session->matchmakeParam.params.end()) {
                    valid = false;
                    break;
                }

                if (mmParam.second != sessionIt->second) {
                    valid = false;
                    break;
                }
            }
        }

        return valid;
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
        session->openParticipation = true;
        session->participationCount = 0;
        session->sessionKey = Buffer(crypto::genKey());
        session->startedTime = std::chrono::system_clock::now();

//        MatchmakeParam mmParam(client.minorVersion);
//        Variant param1;
//        param1.set(Bool(0, true));
//        mmParam.params.insert({String("@SR"), param1});
//        Variant param2;
//        param2.set(Int64(0, 3));
//        mmParam.params.insert({String("@GIR"), param2});
//
//        session->matchmakeParam = mmParam;

        matchmakeSessions.insert({session->id, {session, {client.pid}}});
        sessionInfo = matchmakeSessions[session->id];
        params[0] = session;
    } else {
        auto& session = matchmakeSessions[validSessions.top().second.session->id];
        session.players.insert(client.pid);
        sessionInfo = session;
        params[0] = sessionInfo.session;
    }

    sessionInfo.session->participationCount++;
    clientInfo.joinedGathering = sessionInfo.session;

    sendMsg(client, res, params);

    // We notify other players of the new participant
    for (auto& pid : sessionInfo.players) {
        if (pid == client.pid) continue;
        sendNotification(registeredClients[pid].client, NotificationType::NEW_PARTICIPANT, client.pid,
                         sessionInfo.session->id, client.pid, "", 1);
    }

    // And we also send notifications to the new participant, one for each player already in the session (including himself)
    for (auto& pid : sessionInfo.players) {
        sendNotification(client, NotificationType::NEW_PARTICIPANT, client.pid, sessionInfo.session->id,
                         pid, "", 1);
    }
}

void SplatoonSecureRMC::onDisconnect(prudp::PRUDPAddress address) {
    auto clientIt = registeredClients.find(pidMap[address]);
    if (clientIt != registeredClients.end() && clientIt->second.joinedGathering != nullptr) {
        removePlayerFromSession(clientIt->second.joinedGathering->id, pidMap[address], "", true);
    }

    if (clientIt != registeredClients.end()) registeredClients.erase(pidMap[address]);

    Server::onDisconnect(address);
}

uint32_t SplatoonSecureRMC::getNewGatheringId() {
    // We choose a random number between 1000 and UINT32_MAX, and if it's already taken, we try again.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(1000, UINT32_MAX);

    uint32_t id = dis(gen);
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
    notification.callId = nextReqCallId++;

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
    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) return;

    auto& sessionInfo = sessionIt->second;
    for (auto& pid : sessionInfo.players) {
        auto clientIt = registeredClients.find(pid);

        sendNotification(clientIt->second.client, NotificationType::GATHERING_UNREGISTERED, srcPid, gId, 0, "", 0);

        auto& clientInfo = clientIt->second;
        clientInfo.joinedGathering = nullptr;
    }

    matchmakeSessions.erase(sessionIt);
}

void SplatoonSecureRMC::removePlayerFromSession(uint32_t gId, uint32_t playerPid, const std::string& msg, bool disconnected) {
    auto sessionIt = matchmakeSessions.find(gId);
    if (sessionIt == matchmakeSessions.end()) return;

    if (!disconnected) sendNotification(registeredClients[playerPid].client, NotificationType::PARTICIPATION_ENDED,
                                        playerPid, sessionIt->second.session->id, playerPid, msg, 0);

    sessionIt->second.players.erase(playerPid);
    registeredClients[playerPid].joinedGathering = nullptr;
    if (sessionIt->second.players.empty()) {
        unregisterGathering_internal(gId, playerPid);
        return;
    } else {
        for (auto& pid : sessionIt->second.players) {
            sendNotification(registeredClients[pid].client,
                             (disconnected) ? NotificationType::PARTICIPANT_DISCONNECTED : NotificationType::PARTICIPATION_ENDED,
                             playerPid, sessionIt->second.session->id, playerPid, msg, 0);
        }
    }

    if (sessionIt->second.session->ownerPid == playerPid) {
        // TODO Choose the best player as the new owner
        sessionIt->second.session->ownerPid = *sessionIt->second.players.begin();
        for (auto& pid : sessionIt->second.players) {
            uint64_t msNow = std::chrono::duration_cast<std::chrono::milliseconds>(
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