#ifndef SPLATOON_SERVER_SPLATOONSECURE_HPP
#define SPLATOON_SERVER_SPLATOONSECURE_HPP

#include <memory>
#include "../rmc/server.hpp"
#include "../../db/database.hpp"
#include "../types/common/stationURL.hpp"
#include "../types/common/list.hpp"
#include "../types/splatoonSecure/gathering.hpp"
#include "../types/splatoonSecure/autoMatchmakeParam.hpp"
#include "../types/splatoonSecure/notificationEvent.hpp"
#include "../types/splatoonSecure/joinMatchmakeSessionParam.hpp"
#include "../types/splatoonSecure/createMatchmakeSessionParam.hpp"
#include "../types/splatoonSecure/competitionRankingGetParam.hpp"
#include "../types/splatoonSecure/competitionRankingUploadScoreParam.hpp"
#include "../../sharedState/sharedState.hpp"
#include "../../grpc/channelPool.hpp"

namespace nex::rmc {

class SplatoonSecureRMC : public Server {
public:
    explicit SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                               std::shared_ptr<ss::SharedState> sharedState, uint32_t serverId,
                               int gRCPPoolMaxSize, int gRCPRequestTimeout,
                               const std::shared_ptr<grpc::ChannelCredentials>& grpcCredentials = nullptr);
    ~SplatoonSecureRMC() override = default;

    async::Task<bool> sendNotification(ClientInfo client, NotificationType type, uint32_t srcPid, uint32_t param1, uint32_t param2,
                                       const std::string& strParam, uint32_t param3, bool dontResend = false);
    async::Task<bool> externalRequestProbeInitiationExt(ClientInfo client, std::unique_ptr<StationURL> probe);

    async::Task<uint32_t> getConnectedClientCount() const;
    async::Task<uint32_t> getLobbyCount() const;
    async::Task<std::vector<SessionInfo>> getAllSessions() const;
    async::Task<std::vector<db::DBFestivalTeamTotalsData>> getFestivalTotals(uint32_t festivalId) const;

private:
    async::Task<void> requestProbeInitiationExt(ClientInfo client, Request req,
                                                std::unique_ptr<List<StationURL>> targets, std::unique_ptr<StationURL> probe);
    async::Task<void> reportNatTraversalResult(ClientInfo client, Request req, std::unique_ptr<UInt32> cid,
                                               std::unique_ptr<Bool> result, std::unique_ptr<UInt32> rtt);
    async::Task<void> reportNatProperties(ClientInfo client, Request req,
                                          std::unique_ptr<UInt32> mapping, std::unique_ptr<UInt32> filtering, std::unique_ptr<UInt32> rtt);
    async::Task<void> secure_register(ClientInfo client, Request req,
                                      std::unique_ptr<List<StationURL>> urls);
    async::Task<void> replaceUrl(ClientInfo client, Request req,
                                 std::unique_ptr<StationURL> oldUrl, std::unique_ptr<StationURL> newUrl);
    async::Task<void> sendReport(ClientInfo client, Request req,
                                 std::unique_ptr<UInt32> id, std::unique_ptr<qBuffer> report);
    async::Task<void> unregisterGathering(ClientInfo client, Request req,
                                          std::unique_ptr<UInt32> gId);
    async::Task<void> findBySingleId(ClientInfo client, Request req,
                                     std::unique_ptr<UInt32> id);
    async::Task<void> getSessionUrls(ClientInfo client, Request req,
                                     std::unique_ptr<UInt32> gId);
    async::Task<void> updateSessionHost(ClientInfo client, Request req,
                                        std::unique_ptr<UInt32> gId, std::unique_ptr<Bool> migrateOwner);
    async::Task<void> migrateGatheringOwnership(ClientInfo client, Request req,
                                                std::unique_ptr<UInt32> gId, std::unique_ptr<List<PID>> potentialNewOwners,
                                                std::unique_ptr<Bool> participantsOnly);
    async::Task<void> endParticipation(ClientInfo client, Request req,
                                       std::unique_ptr<UInt32> gId, std::unique_ptr<String> msg);
    async::Task<void> closeParticipation(ClientInfo client, Request req,
                                         std::unique_ptr<UInt32> gId);
    async::Task<void> openParticipation(ClientInfo client, Request req,
                                        std::unique_ptr<UInt32> gId);
    async::Task<void> modifyCurrentGameAttribute(ClientInfo client, Request req,
                                                 std::unique_ptr<UInt32> gId, std::unique_ptr<UInt32> attribIndex,
                                                 std::unique_ptr<UInt32> newValue);
    async::Task<void> getPlayingSessions(ClientInfo client, Request req,
                                         std::unique_ptr<List<PID>> pids);
    async::Task<void> updateProgressScore(ClientInfo client, Request req,
                                          std::unique_ptr<UInt32> gId, std::unique_ptr<UInt8> score);
    async::Task<void> createMatchmakeSessionWithParam(ClientInfo client, Request req,
                                                      std::unique_ptr<CreateMatchmakeSessionParam> param);
    async::Task<void> joinMatchmakeSessionWithParam(ClientInfo client, Request req,
                                                    std::unique_ptr<JoinMatchmakeSessionParam> param);
    async::Task<void> autoMatchmakeWithParam_Postpone(ClientInfo client, Request req,
                                                      std::unique_ptr<AutoMatchmakeParam> param);
    async::Task<void> getCompetitionRankingScore(ClientInfo client, Request req,
                                                 std::unique_ptr<CompetitionRankingGetParam> param);
    async::Task<void> uploadCompetitionRankingScore(ClientInfo client, Request req,
                                                    std::unique_ptr<CompetitionRankingUploadScoreParam> param);

    async::Task<void> onDisconnect(prudp::PRUDPAddress address) override;

    async::Task<uint32_t> getNewGatheringId();
    async::Task<void> unregisterGathering_internal(uint32_t gId, uint32_t srcPid);
//    void addPlayersToSession(uint32_t gId, const std::vector<uint32_t>& playerPids);
    async::Task<void> removePlayerFromSession(uint32_t gId, uint32_t playerPid, const std::string& msg = "", bool disconnected = false);

    std::shared_ptr<db::Database> db;
    std::shared_ptr<ss::SharedState> sharedState;

    uint32_t nextRVConnId = 1;
    std::mutex rvConnIdMutex;
    uint32_t nextReqCallId = 0;
    std::mutex reqCallIdMutex;

    std::shared_ptr<grpcimpl::ChannelPool> channelPool;
    int gRCPRequestTimeout;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_SPLATOONSECURE_HPP
