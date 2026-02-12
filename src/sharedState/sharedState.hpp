#ifndef SPLATOON_SERVER_SHAREDSTATE_HPP
#define SPLATOON_SERVER_SHAREDSTATE_HPP

#include "../logger.hpp"
#include "../util/manualTask.hpp"

// Forward declarations to avoid circular dependency
namespace nex::rmc {
    struct FriendsRegisteredClientInfo;
    struct UserPreference;
}

namespace ss {

enum class SSType {
    LOCAL,
    REDIS
};

enum class Result {
    SUCCESS,
    FAILURE
};

class SharedState {
protected:
    SharedState(std::shared_ptr<Logger::Logger> logger, SSType type, uint32_t serverId, const std::string& publicFacingRPCAddress);

    std::shared_ptr<Logger::Logger> logger;
    SSType type;
    uint32_t serverId;
    std::string publicFacingRPCAddress;

public:
    virtual ~SharedState() = default;

    virtual uint64_t process() = 0;

    virtual bool init() = 0;
    virtual void close() = 0;

    virtual async::ManualTask<std::pair<Result, std::optional<std::string>>> getPublicFacingRPCAddress(uint32_t serverId) = 0;
    virtual async::ManualTask<Result> setFriendsRegisteredClientInfo(const nex::rmc::FriendsRegisteredClientInfo&& info) = 0;
    virtual async::ManualTask<std::pair<Result, std::optional<nex::rmc::FriendsRegisteredClientInfo>>> getFriendsRegisteredClientInfo(uint32_t pid) = 0;
    virtual async::ManualTask<Result> deleteFriendsRegisteredClientInfo(uint32_t pid) = 0;
    virtual async::ManualTask<Result> addFriendToRegisteredClientInfo(uint32_t pid, uint32_t friendPid) = 0;
    virtual async::ManualTask<Result> removeFriendFromRegisteredClientInfo(uint32_t pid, uint32_t friendPid) = 0;
    virtual async::ManualTask<Result> updatePreferenceInRegisteredClientInfo(uint32_t pid, const nex::rmc::UserPreference& preference) = 0;
    virtual async::ManualTask<std::pair<Result, uint64_t>> getRegisteredClientCount() = 0;
};

} // namespace ss

#endif //SPLATOON_SERVER_SHAREDSTATE_HPP