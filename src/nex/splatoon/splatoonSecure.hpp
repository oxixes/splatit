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
#include "../types/splatoonSecure/competitionRankingGetParam.hpp"
#include "../types/splatoonSecure/competitionRankingUploadScoreParam.hpp"

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
    std::vector<std::shared_ptr<Gathering>> joinedGatherings = {};
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
    void requestProbeInitiationExt(ClientInfo client, Request req,
                                   std::unique_ptr<List<StationURL>> targets, std::unique_ptr<StationURL> probe);
    void reportNatTraversalResult(ClientInfo client, Request req, std::unique_ptr<UInt32> cid,
                                  std::unique_ptr<Bool> result, std::unique_ptr<UInt32> rtt);
    void reportNatProperties(ClientInfo client, Request req,
                             std::unique_ptr<UInt32> mapping, std::unique_ptr<UInt32> filtering, std::unique_ptr<UInt32> rtt);
    void secure_register(ClientInfo client, Request req,
                         std::unique_ptr<List<StationURL>> urls);
    void replaceUrl(ClientInfo client, Request req,
                    std::unique_ptr<StationURL> oldUrl, std::unique_ptr<StationURL> newUrl);
    void sendReport(ClientInfo client, Request req,
                    std::unique_ptr<UInt32> id, std::unique_ptr<qBuffer> report);
    void unregisterGathering(ClientInfo client, Request req,
                             std::unique_ptr<UInt32> gId);
    void findBySingleId(ClientInfo client, Request req,
                        std::unique_ptr<UInt32> id);
    void getSessionUrls(ClientInfo client, Request req,
                        std::unique_ptr<UInt32> gId);
    void updateSessionHost(ClientInfo client, Request req,
                           std::unique_ptr<UInt32> gId, std::unique_ptr<Bool> migrateOwner);
    void migrateGatheringOwnership(ClientInfo client, Request req,
                                   std::unique_ptr<UInt32> gId, std::unique_ptr<List<PID>> potentialNewOwners,
                                   std::unique_ptr<Bool> participantsOnly);
    void endParticipation(ClientInfo client, Request req,
                          std::unique_ptr<UInt32> gId, std::unique_ptr<String> msg);
    void closeParticipation(ClientInfo client, Request req,
                            std::unique_ptr<UInt32> gId);
    void openParticipation(ClientInfo client, Request req,
                           std::unique_ptr<UInt32> gId);
    void modifyCurrentGameAttribute(ClientInfo client, Request req,
                                    std::unique_ptr<UInt32> gId, std::unique_ptr<UInt32> attribIndex,
                                    std::unique_ptr<UInt32> newValue);
    void getPlayingSessions(ClientInfo client, Request req,
                            std::unique_ptr<List<PID>> pids);
    void updateProgressScore(ClientInfo client, Request req,
                             std::unique_ptr<UInt32> gId, std::unique_ptr<UInt8> score);
    void createMatchmakeSessionWithParam(ClientInfo client, Request req,
                                         std::unique_ptr<CreateMatchmakeSessionParam> param);
    void joinMatchmakeSessionWithParam(ClientInfo client, Request req,
                                       std::unique_ptr<JoinMatchmakeSessionParam> param);
    void autoMatchmakeWithParam_Postpone(ClientInfo client, Request req, 
                                         std::unique_ptr<AutoMatchmakeParam> param);
    void getCompetitionRankingScore(ClientInfo client, Request req,
                                    std::unique_ptr<CompetitionRankingGetParam> param);
    void uploadCompetitionRankingScore(ClientInfo client, Request req,
                                       std::unique_ptr<CompetitionRankingUploadScoreParam> param);

    void onDisconnect(prudp::PRUDPAddress address) override;

    uint32_t getNewGatheringId();
    void sendNotification(ClientInfo client, NotificationType type, uint32_t srcPid, uint32_t param1, uint32_t param2,
                          const std::string& strParam, uint32_t param3);
    void unregisterGathering_internal(uint32_t gId, uint32_t srcPid);
//    void addPlayersToSession(uint32_t gId, const std::vector<uint32_t>& playerPids);
    void removePlayerFromSession(uint32_t gId, uint32_t playerPid, const std::string& msg = "", bool disconnected = false);

    std::shared_ptr<db::Database> db;

    uint32_t nextRVConnId = 1;
    std::mutex rvConnIdMutex;
    uint32_t nextReqCallId = 0;
    std::mutex reqCallIdMutex;
    std::unordered_map<uint32_t, SplatoonRegisteredClientInfo> registeredClients;
    std::recursive_mutex registeredClientsMutex;
    std::unordered_map<uint32_t, SessionInfo> matchmakeSessions;
    std::recursive_mutex matchmakeSessionsMutex;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_SPLATOONSECURE_HPP
