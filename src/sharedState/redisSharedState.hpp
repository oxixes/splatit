#ifndef SPLATOON_SERVER_REDISSHAREDSTATE_HPP
#define SPLATOON_SERVER_REDISSHAREDSTATE_HPP

#include <hiredis/hiredis.h>
#include <hiredis/hiredis_ssl.h>
#include <thread>
#include <queue>
#include <set>
#include <condition_variable>
#include <functional>
#include <atomic>

#include "sharedState.hpp"
#include "../socket/socket.hpp"
#include "../nex/prudp/server.hpp"
#include "../nex/rmc/server.hpp"

namespace ss {

// Key prefix for all Redis keys used by this application
constexpr const char* REDIS_KEY_PREFIX = "splatit:";

struct RedisConfig {
    std::string host;
    uint16_t port;
    std::string password;
    int database;
    uint32_t connectionTimeoutMs;
    uint32_t commandTimeoutMs;
    bool useSSL;
    std::string caCertPath;
    std::string certPath;
    std::string keyPath;
    uint32_t workerThreads;
    uint32_t clientTTLSeconds;  // TTL for registered clients
};

class RedisSharedState : public SharedState {
public:
    explicit RedisSharedState(std::shared_ptr<Logger::Logger> logger, RedisConfig config, uint32_t serverId, const std::string& publicFacingRPCAddress);
    ~RedisSharedState() override;

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
    async::ManualTask<std::pair<Result, uint64_t>> getFriendsRegisteredClientCount() override;
    async::ManualTask<Result> setSplatoonRegisteredClientInfo(const nex::rmc::SplatoonRegisteredClientInfo&& info) override;
    async::ManualTask<std::pair<Result, std::optional<nex::rmc::SplatoonRegisteredClientInfo>>> getSplatoonRegisteredClientInfo(uint32_t pid) override;
    async::ManualTask<Result> deleteSplatoonRegisteredClientInfo(uint32_t pid) override;
    async::ManualTask<Result> updateSplatoonRegisteredClientURLs(uint32_t pid, const std::vector<nex::rmc::StationURL>& urls) override;
    async::ManualTask<Result> updateSplatoonRegisteredClientLastReportedNATProperties(uint32_t pid, const nex::rmc::NATProperties& natProperties) override;
    async::ManualTask<Result> setSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) override;
    async::ManualTask<std::pair<Result, std::optional<nex::rmc::SessionInfo>>> getSplatoonMatchmakeSession(uint32_t gId) override;
    async::ManualTask<Result> deleteSplatoonMatchmakeSession(uint32_t gId) override;
    async::ManualTask<std::pair<Result, std::unordered_map<uint32_t, nex::rmc::SessionInfo>>> getAllSplatoonMatchmakeSessions() override;
    async::ManualTask<Result> updateSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) override;
    async::ManualTask<Result> addPlayersToSplatoonMatchmakeSession(uint32_t gId, const std::vector<uint32_t>& playerPids) override;
    async::ManualTask<Result> removePlayerFromSplatoonMatchmakeSession(uint32_t gId, uint32_t playerPid) override;

private:
    struct Task {
        std::function<void(redisContext*)> operation;
    };

    RedisConfig config;
    std::vector<std::thread> workers;
    std::queue<Task> taskQueue;
    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::atomic<bool> running;
    redisSSLContext* sslContext;

    // Track PIDs registered by this server instance for TTL refresh
    std::set<uint32_t> registeredPIDs;
    std::mutex registeredPIDsMutex;
    std::chrono::steady_clock::time_point lastRefreshTime;

    // Local cache of client info for recreation after reconnection
    std::map<uint32_t, nex::rmc::FriendsRegisteredClientInfo> localClientCache;
    std::mutex localClientCacheMutex;
    std::map<uint32_t, nex::rmc::SplatoonRegisteredClientInfo> localSplatoonClientCache;
    std::mutex localSplatoonClientCacheMutex;
    std::map<uint32_t, nex::rmc::SessionInfo> localSplatoonSessionCache;
    std::mutex localSplatoonSessionCacheMutex;

    // Expiration listener thread
    std::thread expirationListenerThread;

    void workerThread(uint32_t threadId);
    void expirationListener();
    void recreateAllClients();
    redisContext* createConnection() const;
    static void closeConnection(redisContext* ctx);
    void refreshClientTTLs();
    void setTTL(redisContext* ctx, uint32_t pid);
    void setSplatoonClientTTL(redisContext* ctx, uint32_t pid) const;
    void setSplatoonSessionTTL(redisContext* ctx, uint32_t gId) const;
    void incrementClientCount(redisContext* ctx) const;
    void decrementClientCount(redisContext* ctx) const;
    void incrementSplatoonClientCount(redisContext* ctx) const;
    void decrementSplatoonClientCount(redisContext* ctx) const;
    void incrementSplatoonGatheringCount(redisContext* ctx) const;
    void decrementSplatoonGatheringCount(redisContext* ctx) const;

    // Serialization helpers
    static std::string serializeIPv4Addr(const sock::IPv4Addr& addr);
    static sock::IPv4Addr deserializeIPv4Addr(const std::string& str);
    static std::string serializePRUDPAddress(const nex::prudp::PRUDPAddress& addr);
    static nex::prudp::PRUDPAddress deserializePRUDPAddress(const std::string& str);
    static std::string serializeClientInfo(const nex::rmc::ClientInfo& info);
    static nex::rmc::ClientInfo deserializeClientInfo(const std::string& str);
    static std::string serializeUserPreference(const nex::rmc::UserPreference& pref);
    static nex::rmc::UserPreference deserializeUserPreference(const std::string& str);
    static std::string serializeStationURL(const nex::rmc::StationURL& url);
    static nex::rmc::StationURL deserializeStationURL(const std::string& str, uint8_t minorVersion);
    static std::string serializeMatchmakeSession(const nex::rmc::MatchmakeSession& session);
    static nex::rmc::MatchmakeSession deserializeMatchmakeSession(const std::string& str, uint8_t minorVersion);
};

} // namespace ss

#endif //SPLATOON_SERVER_REDISSHAREDSTATE_HPP
