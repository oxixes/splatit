#ifndef SPLATOON_SERVER_LOCALSHAREDSTATE_HPP
#define SPLATOON_SERVER_LOCALSHAREDSTATE_HPP

#include "sharedState.hpp"

namespace ss {

class LocalSharedState : public SharedState {
public:
    explicit LocalSharedState(std::shared_ptr<Logger::Logger> logger, uint32_t serverId, const std::string& publicFacingRPCAddress);

    uint64_t process() override;

    bool init() override;
    void close() override;

    async::ManualTask<std::pair<Result, std::optional<std::string>>> getPublicFacingRPCAddress(uint32_t serverId) override;
    async::ManualTask<Result> setFriendsRegisteredClientInfo(const nex::rmc::FriendsRegisteredClientInfo&& info) override;
    async::ManualTask<std::pair<Result, std::optional<nex::rmc::FriendsRegisteredClientInfo>>> getFriendsRegisteredClientInfo(uint32_t pid) override;
    async::ManualTask<Result> deleteFriendsRegisteredClientInfo(uint32_t pid) override;
    async::ManualTask<Result> addFriendToRegisteredClientInfo(uint32_t pid, uint32_t friendPid) override;
    async::ManualTask<Result> removeFriendFromRegisteredClientInfo(uint32_t pid, uint32_t friendPid) override;
    async::ManualTask<Result> updatePreferenceInRegisteredFriendsClientInfo(uint32_t pid, const nex::rmc::UserPreference& preference) override;
    async::ManualTask<std::pair<Result, uint32_t>> getFriendsRegisteredClientCount() override;

    async::ManualTask<Result> setSplatoonRegisteredClientInfo(const nex::rmc::SplatoonRegisteredClientInfo&& info) override;
    async::ManualTask<std::pair<Result, std::optional<nex::rmc::SplatoonRegisteredClientInfo>>> getSplatoonRegisteredClientInfo(uint32_t pid) override;
    async::ManualTask<Result> deleteSplatoonRegisteredClientInfo(uint32_t pid) override;
    async::ManualTask<Result> updateSplatoonRegisteredClientURLs(uint32_t pid, const std::vector<nex::rmc::StationURL>& urls) override;
    async::ManualTask<Result> updateSplatoonRegisteredClientLastReportedNATProperties(uint32_t pid, const nex::rmc::NATProperties& natProperties) override;
    async::ManualTask<std::pair<Result, uint32_t>> getSplatoonRegisteredClientCount() override;

    async::ManualTask<Result> setSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) override;
    async::ManualTask<std::pair<Result, std::optional<nex::rmc::SessionInfo>>> getSplatoonMatchmakeSession(uint32_t gId) override;
    async::ManualTask<Result> deleteSplatoonMatchmakeSession(uint32_t gId) override;
    async::ManualTask<std::pair<Result, std::unordered_map<uint32_t, nex::rmc::SessionInfo>>> getAllSplatoonMatchmakeSessions() override;
    async::ManualTask<Result> updateSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) override;
    async::ManualTask<Result> addPlayersToSplatoonMatchmakeSession(uint32_t gId, const std::vector<uint32_t>& playerPids) override;
    async::ManualTask<Result> removePlayerFromSplatoonMatchmakeSession(uint32_t gId, uint32_t playerPid) override;
    async::ManualTask<std::pair<Result, uint32_t>> getSplatoonMatchmakeSessionCount() override;

private:
    std::unordered_map<uint32_t, nex::rmc::FriendsRegisteredClientInfo> friendsRegisteredClientInfos;
    std::unordered_map<uint32_t, nex::rmc::SplatoonRegisteredClientInfo> splatoonRegisteredClientInfos;
    std::unordered_map<uint32_t, nex::rmc::SessionInfo> splatoonMatchmakeSessions;
    std::mutex mutex;
};

} // namespace ss

#endif //SPLATOON_SERVER_LOCALSHAREDSTATE_HPP
