#ifndef SPLATOON_SERVER_SPLATOONSECURE_HPP
#define SPLATOON_SERVER_SPLATOONSECURE_HPP

#include <memory>
#include "../rmc/server.hpp"
#include "../../db/database.hpp"
#include "../types/common/stationURL.hpp"
#include "../types/common/list.hpp"
#include "../types/splatoonSecure/gathering.hpp"
#include "../types/splatoonSecure/matchmakeSession.hpp"
#include "../types/splatoonSecure/autoMatchmakeParam.hpp"
#include "../types/splatoonSecure/notificationEvent.hpp"
#include "../types/splatoonSecure/joinMatchmakeSessionParam.hpp"
#include "../types/splatoonSecure/createMatchmakeSessionParam.hpp"

namespace nex::rmc {

struct NATProperties {
    uint32_t mapping;
    uint32_t filtering;
    uint32_t rtt;
};

struct SplatoonRegisteredClientInfo {
    ClientInfo client;
    std::vector<StationURL> urls;
    StationURL publicUrl;
    uint32_t rvConnId;
    std::shared_ptr<Gathering> joinedGathering = nullptr;
    NATProperties lastReportedNATProperties;
};

struct SessionInfo {
    std::shared_ptr<MatchmakeSession> session;
    std::set<uint32_t> players;
};

class SplatoonSecureRMC : public Server {
public:
    explicit SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db);
    ~SplatoonSecureRMC() override = default;

private:
    void requestProbeInitiationExt(ClientInfo client, Request req, List<StationURL> targets, StationURL probe);
    void reportNatTraversalResult(ClientInfo client, Request req, UInt32 cid, Bool result, UInt32 rtt);
    void reportNatProperties(ClientInfo client, Request req, UInt32 mapping, UInt32 filtering, UInt32 rtt);
    void secure_register(ClientInfo client, Request req, List<StationURL> urls);
    void replaceUrl(ClientInfo client, Request req, StationURL oldUrl, StationURL newUrl);
    void sendReport(ClientInfo client, Request req, UInt32 id, qBuffer report);
    void unregisterGathering(ClientInfo client, Request req, UInt32 gId);
    void findBySingleId(ClientInfo client, Request req, UInt32 id);
    void getSessionUrls(ClientInfo client, Request req, UInt32 gId);
    void updateSessionHost(ClientInfo client, Request req, UInt32 gId, Bool migrateOwner);
    void endParticipation(ClientInfo client, Request req, UInt32 gId, String msg);
    void closeParticipation(ClientInfo client, Request req, UInt32 gId);
    void openParticipation(ClientInfo client, Request req, UInt32 gId);
    void getPlayingSessions(ClientInfo client, Request req, List<PID> pids);
    void updateProgressScore(ClientInfo client, Request req, UInt32 gId, UInt8 score);
    void createMatchmakeSessionWithParam(ClientInfo client, Request req, CreateMatchmakeSessionParam param);
    void joinMatchmakeSessionWithParam(ClientInfo client, Request req, JoinMatchmakeSessionParam param);
    void autoMatchmakeWithParam_Postpone(ClientInfo sessionInfo, Request req, AutoMatchmakeParam param);

    void onDisconnect(prudp::PRUDPAddress address) override;

    uint32_t getNewGatheringId();
    void sendNotification(ClientInfo client, NotificationType type, uint32_t srcPid, uint32_t param1, uint32_t param2,
                          const std::string& strParam, uint32_t param3);
    void unregisterGathering_internal(uint32_t gId, uint32_t srcPid);
//    void addPlayersToSession(uint32_t gId, const std::vector<uint32_t>& playerPids);
    void removePlayerFromSession(uint32_t gId, uint32_t playerPid, const std::string& msg = "", bool disconnected = false, bool switching = false);

    std::shared_ptr<db::Database> db;

    uint32_t nextRVConnId = 1;
    uint32_t nextReqCallId = 0;
    std::unordered_map<uint32_t, SplatoonRegisteredClientInfo> registeredClients;
    std::unordered_map<uint32_t, SessionInfo> matchmakeSessions;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_SPLATOONSECURE_HPP
