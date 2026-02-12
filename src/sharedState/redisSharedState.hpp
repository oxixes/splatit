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
    async::ManualTask<Result> updatePreferenceInRegisteredClientInfo(uint32_t pid, const nex::rmc::UserPreference& preference) override;
    async::ManualTask<std::pair<Result, uint64_t>> getRegisteredClientCount() override;

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

    // Expiration listener thread
    std::thread expirationListenerThread;

    void workerThread(uint32_t threadId);
    void expirationListener();
    void recreateAllClients();
    redisContext* createConnection() const;
    static void closeConnection(redisContext* ctx);
    void refreshClientTTLs();
    void setTTL(redisContext* ctx, uint32_t pid);
    void incrementClientCount(redisContext* ctx) const;
    void decrementClientCount(redisContext* ctx) const;

    // Serialization helpers
    static std::string serializeIPv4Addr(const sock::IPv4Addr& addr);
    static sock::IPv4Addr deserializeIPv4Addr(const std::string& str);
    static std::string serializePRUDPAddress(const nex::prudp::PRUDPAddress& addr);
    static nex::prudp::PRUDPAddress deserializePRUDPAddress(const std::string& str);
    static std::string serializeClientInfo(const nex::rmc::ClientInfo& info);
    static nex::rmc::ClientInfo deserializeClientInfo(const std::string& str);
    static std::string serializeUserPreference(const nex::rmc::UserPreference& pref);
    static nex::rmc::UserPreference deserializeUserPreference(const std::string& str);
};

} // namespace ss

#endif //SPLATOON_SERVER_REDISSHAREDSTATE_HPP