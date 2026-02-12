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
    async::ManualTask<Result> updatePreferenceInRegisteredClientInfo(uint32_t pid, const nex::rmc::UserPreference& preference) override;
    async::ManualTask<std::pair<Result, uint64_t>> getRegisteredClientCount() override;

private:
    std::unordered_map<uint32_t, nex::rmc::FriendsRegisteredClientInfo> friendsRegisteredClientInfos;
    std::mutex mutex;
};

} // namespace ss

#endif //SPLATOON_SERVER_LOCALSHAREDSTATE_HPP