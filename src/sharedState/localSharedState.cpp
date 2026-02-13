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
    task->complete(std::make_pair(Result::SUCCESS, std::nullopt));
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

async::ManualTask<Result> LocalSharedState::updatePreferenceInRegisteredFriendsClientInfo(const uint32_t pid, const nex::rmc::UserPreference& preference) {
    std::scoped_lock lock(mutex);
    auto it = friendsRegisteredClientInfos.find(pid);
    if (it != friendsRegisteredClientInfos.end()) {
        it->second.userData.preference = preference;
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);

    return *task;
}

async::ManualTask<std::pair<Result, uint64_t>> LocalSharedState::getFriendsRegisteredClientCount() {
    std::scoped_lock lock(mutex);
    uint64_t count = friendsRegisteredClientInfos.size();

    const auto task = std::make_shared<async::ManualTask<std::pair<Result, uint64_t>>>();
    task->complete(std::make_pair(Result::SUCCESS, count));

    return *task;
}

async::ManualTask<Result> LocalSharedState::setSplatoonRegisteredClientInfo(
    const nex::rmc::SplatoonRegisteredClientInfo&& info) {
    std::scoped_lock lock(mutex);
    splatoonRegisteredClientInfos[info.client.pid] = info;

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

async::ManualTask<std::pair<Result, std::optional<nex::rmc::SplatoonRegisteredClientInfo>>>
LocalSharedState::getSplatoonRegisteredClientInfo(const uint32_t pid) {
    std::scoped_lock lock(mutex);

    const auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<nex::rmc::SplatoonRegisteredClientInfo>>>>();
    auto it = splatoonRegisteredClientInfos.find(pid);
    if (it == splatoonRegisteredClientInfos.end()) {
        task->complete(std::make_pair(Result::SUCCESS, std::nullopt));
        return *task;
    }

    auto clientInfo = it->second;
    for (auto& gathering : clientInfo.joinedGatherings) {
        if (!gathering) {
            continue;
        }

        auto sessionIt = splatoonMatchmakeSessions.find(gathering->id);
        if (sessionIt != splatoonMatchmakeSessions.end()) {
            gathering = sessionIt->second.session;
        }
    }

    task->complete(std::make_pair(Result::SUCCESS, std::move(clientInfo)));
    return *task;
}

async::ManualTask<Result> LocalSharedState::deleteSplatoonRegisteredClientInfo(const uint32_t pid) {
    std::scoped_lock lock(mutex);
    auto it = splatoonRegisteredClientInfos.find(pid);
    if (it != splatoonRegisteredClientInfos.end()) {
        for (const auto& gathering : it->second.joinedGatherings) {
            if (!gathering) {
                continue;
            }

            auto sessionIt = splatoonMatchmakeSessions.find(gathering->id);
            if (sessionIt == splatoonMatchmakeSessions.end()) {
                continue;
            }

            sessionIt->second.players.erase(pid);
            sessionIt->second.session->participationCount = static_cast<uint32_t>(sessionIt->second.players.size());
        }
    }

    splatoonRegisteredClientInfos.erase(pid);

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

async::ManualTask<Result> LocalSharedState::updateSplatoonRegisteredClientURLs(
    const uint32_t pid, const std::vector<nex::rmc::StationURL>& urls) {
    std::scoped_lock lock(mutex);
    auto it = splatoonRegisteredClientInfos.find(pid);
    if (it != splatoonRegisteredClientInfos.end()) {
        it->second.urls = urls;
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

async::ManualTask<Result> LocalSharedState::updateSplatoonRegisteredClientLastReportedNATProperties(
    const uint32_t pid, const nex::rmc::NATProperties& natProperties) {
    std::scoped_lock lock(mutex);
    auto it = splatoonRegisteredClientInfos.find(pid);
    if (it != splatoonRegisteredClientInfos.end()) {
        it->second.lastReportedNATProperties = natProperties;
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

async::ManualTask<Result> LocalSharedState::setSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) {
    std::scoped_lock lock(mutex);
    if (!sessionInfo.session) {
        const auto task = std::make_shared<async::ManualTask<Result>>();
        task->complete(Result::FAILURE);
        return *task;
    }

    uint32_t gId = sessionInfo.session->id;
    std::shared_ptr<nex::rmc::MatchmakeSession> session = std::make_shared<nex::rmc::MatchmakeSession>(*sessionInfo.session);
    splatoonMatchmakeSessions[gId] = {session, sessionInfo.players};
    splatoonMatchmakeSessions[gId].session->participationCount = static_cast<uint32_t>(splatoonMatchmakeSessions[gId].players.size());

    for (auto& [pid, clientInfo] : splatoonRegisteredClientInfos) {
        auto gatheringIt = std::ranges::find_if(clientInfo.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
            return gathering && gathering->id == gId;
        });

        bool shouldHaveGathering = splatoonMatchmakeSessions[gId].players.contains(pid);
        if (shouldHaveGathering && gatheringIt == clientInfo.joinedGatherings.end()) {
            clientInfo.joinedGatherings.push_back(splatoonMatchmakeSessions[gId].session);
        } else if (!shouldHaveGathering && gatheringIt != clientInfo.joinedGatherings.end()) {
            clientInfo.joinedGatherings.erase(gatheringIt);
        } else if (shouldHaveGathering && gatheringIt != clientInfo.joinedGatherings.end()) {
            *gatheringIt = splatoonMatchmakeSessions[gId].session;
        }
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

async::ManualTask<std::pair<Result, std::optional<nex::rmc::SessionInfo>>> LocalSharedState::getSplatoonMatchmakeSession(const uint32_t gId) {
    std::scoped_lock lock(mutex);
    const auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<nex::rmc::SessionInfo>>>>();
    auto it = splatoonMatchmakeSessions.find(gId);
    if (it == splatoonMatchmakeSessions.end()) {
        task->complete(std::make_pair(Result::SUCCESS, std::nullopt));
    } else {
        task->complete(std::make_pair(Result::SUCCESS, it->second));
    }
    return *task;
}

async::ManualTask<Result> LocalSharedState::deleteSplatoonMatchmakeSession(const uint32_t gId) {
    std::scoped_lock lock(mutex);
    auto sessionIt = splatoonMatchmakeSessions.find(gId);
    if (sessionIt != splatoonMatchmakeSessions.end()) {
        for (const auto& pid : sessionIt->second.players) {
            auto clientIt = splatoonRegisteredClientInfos.find(pid);
            if (clientIt == splatoonRegisteredClientInfos.end()) {
                continue;
            }

            auto gatheringIt = std::ranges::find_if(clientIt->second.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
                return gathering && gathering->id == gId;
            });

            if (gatheringIt != clientIt->second.joinedGatherings.end()) {
                clientIt->second.joinedGatherings.erase(gatheringIt);
            }
        }
    }

    splatoonMatchmakeSessions.erase(gId);

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

async::ManualTask<std::pair<Result, std::unordered_map<uint32_t, nex::rmc::SessionInfo>>>
LocalSharedState::getAllSplatoonMatchmakeSessions() {
    std::scoped_lock lock(mutex);
    const auto task = std::make_shared<async::ManualTask<std::pair<Result, std::unordered_map<uint32_t, nex::rmc::SessionInfo>>>>();
    task->complete(std::make_pair(Result::SUCCESS, splatoonMatchmakeSessions));
    return *task;
}

async::ManualTask<Result> LocalSharedState::updateSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) {
    return setSplatoonMatchmakeSession(std::move(sessionInfo));
}

async::ManualTask<Result> LocalSharedState::addPlayersToSplatoonMatchmakeSession(
    const uint32_t gId, const std::vector<uint32_t>& playerPids) {
    std::scoped_lock lock(mutex);

    auto sessionIt = splatoonMatchmakeSessions.find(gId);
    if (sessionIt == splatoonMatchmakeSessions.end()) {
        const auto task = std::make_shared<async::ManualTask<Result>>();
        task->complete(Result::FAILURE);
        return *task;
    }

    for (uint32_t pid : playerPids) {
        sessionIt->second.players.insert(pid);
        auto clientIt = splatoonRegisteredClientInfos.find(pid);
        if (clientIt == splatoonRegisteredClientInfos.end()) {
            continue;
        }

        auto gatheringIt = std::ranges::find_if(clientIt->second.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
            return gathering && gathering->id == gId;
        });
        if (gatheringIt == clientIt->second.joinedGatherings.end()) {
            clientIt->second.joinedGatherings.push_back(sessionIt->second.session);
        } else {
            *gatheringIt = sessionIt->second.session;
        }
    }

    sessionIt->second.session->participationCount = static_cast<uint32_t>(sessionIt->second.players.size());

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

async::ManualTask<Result> LocalSharedState::removePlayerFromSplatoonMatchmakeSession(const uint32_t gId, const uint32_t playerPid) {
    std::scoped_lock lock(mutex);

    auto sessionIt = splatoonMatchmakeSessions.find(gId);
    if (sessionIt == splatoonMatchmakeSessions.end()) {
        const auto task = std::make_shared<async::ManualTask<Result>>();
        task->complete(Result::FAILURE);
        return *task;
    }

    sessionIt->second.players.erase(playerPid);
    sessionIt->second.session->participationCount = static_cast<uint32_t>(sessionIt->second.players.size());

    auto clientIt = splatoonRegisteredClientInfos.find(playerPid);
    if (clientIt != splatoonRegisteredClientInfos.end()) {
        auto gatheringIt = std::ranges::find_if(clientIt->second.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
            return gathering && gathering->id == gId;
        });
        if (gatheringIt != clientIt->second.joinedGatherings.end()) {
            clientIt->second.joinedGatherings.erase(gatheringIt);
        }
    }

    const auto task = std::make_shared<async::ManualTask<Result>>();
    task->complete(Result::SUCCESS);
    return *task;
}

} // namespace ss
