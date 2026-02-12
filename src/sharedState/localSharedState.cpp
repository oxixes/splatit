#include "localSharedState.hpp"
#include "../nex/friends/friendsSecure.hpp"

namespace ss {

LocalSharedState::LocalSharedState(std::shared_ptr<Logger::Logger> logger, const uint32_t serverId, const std::string& publicFacingRPCAddress) :
    SharedState(std::move(logger), SSType::LOCAL, serverId, publicFacingRPCAddress) {}

bool LocalSharedState::init() {
    return true;
}

void LocalSharedState::close() {
    // No resources to clean up for local shared state.
}

// Local shared state does not need to refresh any data, so the time it takes for the next refresh is effectively infinite.
uint64_t LocalSharedState::process() {
    return std::numeric_limits<uint64_t>::max();
}

async::ManualTask<std::pair<Result, std::optional<std::string>>> LocalSharedState::getPublicFacingRPCAddress(const uint32_t serverId) {
    if (serverId == this->serverId) {
        const auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<std::string>>>>();
        task->complete(std::make_pair(Result::SUCCESS, publicFacingRPCAddress));
        return *task;
    }

    const auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<std::string>>>>();
    task->complete(std::make_pair(Result::FAILURE, std::nullopt));
    return *task;
}

async::ManualTask<Result> LocalSharedState::setFriendsRegisteredClientInfo(const nex::rmc::FriendsRegisteredClientInfo&& info) {
    std::scoped_lock lock(mutex);
    friendsRegisteredClientInfos[info.userData.pid] = info;

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);

    return *task;
}

async::ManualTask<std::pair<Result, std::optional<nex::rmc::FriendsRegisteredClientInfo>>> LocalSharedState::getFriendsRegisteredClientInfo(const uint32_t pid) {
    std::scoped_lock lock(mutex);

    const auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<nex::rmc::FriendsRegisteredClientInfo>>>>();
    auto it = friendsRegisteredClientInfos.find(pid);
    if (it != friendsRegisteredClientInfos.end()) {
        task->complete(std::make_pair(Result::SUCCESS, it->second));
    } else {
        task->complete(std::make_pair(Result::SUCCESS, std::nullopt));
    }

    return *task;
}

async::ManualTask<Result> LocalSharedState::deleteFriendsRegisteredClientInfo(const uint32_t pid) {
    std::scoped_lock lock(mutex);
    friendsRegisteredClientInfos.erase(pid);

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);

    return *task;
}

async::ManualTask<Result> LocalSharedState::addFriendToRegisteredClientInfo(const uint32_t pid, const uint32_t friendPid) {
    std::scoped_lock lock(mutex);
    auto it = friendsRegisteredClientInfos.find(pid);
    if (it != friendsRegisteredClientInfos.end()) {
        it->second.friends.insert(friendPid);
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);

    return *task;
}

async::ManualTask<Result> LocalSharedState::removeFriendFromRegisteredClientInfo(const uint32_t pid, const uint32_t friendPid) {
    std::scoped_lock lock(mutex);
    auto it = friendsRegisteredClientInfos.find(pid);
    if (it != friendsRegisteredClientInfos.end()) {
        it->second.friends.erase(friendPid);
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);

    return *task;
}

async::ManualTask<Result> LocalSharedState::updatePreferenceInRegisteredClientInfo(const uint32_t pid, const nex::rmc::UserPreference& preference) {
    std::scoped_lock lock(mutex);
    auto it = friendsRegisteredClientInfos.find(pid);
    if (it != friendsRegisteredClientInfos.end()) {
        it->second.userData.preference = preference;
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);

    return *task;
}

async::ManualTask<std::pair<Result, uint64_t>> LocalSharedState::getRegisteredClientCount() {
    std::scoped_lock lock(mutex);
    uint64_t count = friendsRegisteredClientInfos.size();

    const auto task = std::make_shared<async::ManualTask<std::pair<Result, uint64_t>>>();
    task->complete(std::make_pair(Result::SUCCESS, count));

    return *task;
}

} // namespace ss