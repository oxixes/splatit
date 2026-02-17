#include "redisSharedState.hpp"
#include "../socket/socket.hpp"
#include "../nex/prudp/server.hpp"
#include "../nex/friends/friendsSecure.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>

// Helper macro to validate Redis context before operations
#define VALIDATE_REDIS_CONTEXT(ctx, operation_name, return_statement) \
    do { \
        if (!ctx || ctx->err != 0) { \
            logger->log(Logger::level::FAILURE, Logger::group::REDIS, \
                operation_name + std::string(" failed: invalid or disconnected context")); \
            return_statement; \
        } \
    } while(0)

namespace ss {

namespace {

bool parseRedisUint64(const redisReply* reply, uint64_t& value) {
    if (!reply) {
        return false;
    }

    if (reply->type == REDIS_REPLY_INTEGER) {
        if (reply->integer < 0) {
            return false;
        }
        value = static_cast<uint64_t>(reply->integer);
        return true;
    }

    if (reply->type == REDIS_REPLY_STRING && reply->str) {
        try {
            value = std::stoull(reply->str);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    return false;
}

bool countKeysByPattern(redisContext* ctx, const std::string& pattern, uint32_t& count, const std::string& exclusionPattern = "") {
    constexpr const char* scanCountScript = R"LUASCRIPT(
local result = redis.call('SCAN', ARGV[1], 'MATCH', ARGV[2], 'COUNT', ARGV[3])
result[2] = #result[2]
return result
)LUASCRIPT";

    constexpr const char* scanCountWithExclusionScript = R"LUASCRIPT(
local result = redis.call('SCAN', ARGV[1], 'MATCH', ARGV[2], 'COUNT', ARGV[3])
local filtered = {}
for i, key in ipairs(result[2]) do
    if not string.match(key, ARGV[4]) then
        table.insert(filtered, key)
    end
end
result[2] = #filtered
return result
)LUASCRIPT";

    std::string cursor = "0";
    count = 0;
    const char* scriptToUse = exclusionPattern.empty() ? scanCountScript : scanCountWithExclusionScript;

    do {
        redisReply* reply = nullptr;
        if (exclusionPattern.empty()) {
            reply = static_cast<redisReply*>(redisCommand(
                ctx, "EVAL %s 0 %s %s %u",
                scriptToUse, cursor.c_str(), pattern.c_str(), REDIS_SCAN_COUNT_CHUNK_SIZE));
        } else {
            reply = static_cast<redisReply*>(redisCommand(
                ctx, "EVAL %s 0 %s %s %u %s",
                scriptToUse, cursor.c_str(), pattern.c_str(), REDIS_SCAN_COUNT_CHUNK_SIZE, exclusionPattern.c_str()));
        }

        if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements != 2) {
            if (reply) {
                freeReplyObject(reply);
            }
            return false;
        }

        if (reply->element[0]->type == REDIS_REPLY_STRING && reply->element[0]->str) {
            cursor.assign(reply->element[0]->str, reply->element[0]->len);
        } else if (reply->element[0]->type == REDIS_REPLY_INTEGER) {
            cursor = std::to_string(reply->element[0]->integer);
        } else {
            freeReplyObject(reply);
            return false;
        }

        uint64_t batchCount = 0;
        if (!parseRedisUint64(reply->element[1], batchCount)) {
            freeReplyObject(reply);
            return false;
        }

        count += batchCount;
        freeReplyObject(reply);
    } while (cursor != "0");

    return true;
}

} // namespace

RedisSharedState::RedisSharedState(std::shared_ptr<Logger::Logger> logger, RedisConfig config, uint32_t serverId, const std::string& publicFacingRPCAddress)
    : SharedState(std::move(logger), SSType::REDIS, serverId, publicFacingRPCAddress), config(std::move(config)), running(false), sslContext(nullptr),
      lastRefreshTime(std::chrono::steady_clock::now()) {}

RedisSharedState::~RedisSharedState() {
    if (running) {
        RedisSharedState::close();
    }
}

bool RedisSharedState::init() {
    logger->log(Logger::level::INFO, Logger::group::REDIS, "Initializing Redis shared state");

    // Initialize SSL context if needed
    if (config.useSSL) {
        logger->log(Logger::level::INFO, Logger::group::REDIS, "Initializing SSL support");

        redisInitOpenSSL();

        redisSSLContextError sslError;
        sslContext = redisCreateSSLContext(
            config.caCertPath.empty() ? nullptr : config.caCertPath.c_str(),
            nullptr, // CA path (directory)
            config.certPath.empty() ? nullptr : config.certPath.c_str(),
            config.keyPath.empty() ? nullptr : config.keyPath.c_str(),
            nullptr, // Server name (SNI)
            &sslError
        );

        if (!sslContext) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "Failed to create SSL context: " + std::string(redisSSLContextGetError(sslError)));
            return false;
        }

        logger->log(Logger::level::INFO, Logger::group::REDIS, "SSL context created successfully");
    }

    // Test connection
    redisContext* testCtx = createConnection();
    if (!testCtx) {
        logger->log(Logger::level::FAILURE, Logger::group::REDIS, "Failed to create initial connection");
        if (sslContext) {
            redisFreeSSLContext(sslContext);
            sslContext = nullptr;
        }
        return false;
    }

    closeConnection(testCtx);

    logger->log(Logger::level::INFO, Logger::group::REDIS,
        "Starting " + std::to_string(config.workerThreads) + " worker threads");

    // Start worker threads
    running = true;
    for (uint32_t i = 0; i < config.workerThreads; ++i) {
        workers.emplace_back(&RedisSharedState::workerThread, this, i);
    }

    logger->log(Logger::level::INFO, Logger::group::REDIS, "Redis shared state initialized successfully");
    return true;
}

void RedisSharedState::close() {
    if (!running) {
        return;
    }

    logger->log(Logger::level::INFO, Logger::group::REDIS, "Closing Redis shared state");

    running = false;
    queueCV.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();

    if (sslContext) {
        redisFreeSSLContext(sslContext);
        sslContext = nullptr;
    }

    logger->log(Logger::level::INFO, Logger::group::REDIS, "Redis shared state closed");
}

uint64_t RedisSharedState::process() {
    // Calculate refresh interval: refresh at 50% of TTL to ensure we don't lose data
    uint64_t refreshIntervalMs = (static_cast<uint64_t>(config.clientTTLSeconds) * 1000) / 2;

    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastRefresh = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefreshTime).count();

    // Check if it's time to refresh
    if (timeSinceLastRefresh >= refreshIntervalMs) {
        refreshClientTTLs();
        lastRefreshTime = now;

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "TTL refresh completed, next refresh in " + std::to_string(refreshIntervalMs) + "ms");
    }

    // Return time until next refresh
    uint64_t timeUntilNextRefresh = refreshIntervalMs - timeSinceLastRefresh;
    return timeUntilNextRefresh > 0 ? timeUntilNextRefresh : 0;
}

void RedisSharedState::workerThread(uint32_t threadId) {
    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
        "Worker thread " + std::to_string(threadId) + " started");

    redisContext* ctx = createConnection();
    if (!ctx) {
        logger->log(Logger::level::FAILURE, Logger::group::REDIS,
            "Worker thread " + std::to_string(threadId) + " failed to create connection");
        return;
    }

    while (running) {
        Task task;
        {
            std::unique_lock lock(queueMutex);
            queueCV.wait(lock, [this] { return !taskQueue.empty() || !running; });

            if (!running && taskQueue.empty()) {
                break;
            }

            if (!taskQueue.empty()) {
                task = std::move(taskQueue.front());
                taskQueue.pop();
            } else {
                continue;
            }
        }

        // Check connection health before executing task
        if (ctx->err != 0) {
            logger->log(Logger::level::WARN, Logger::group::REDIS,
                "Worker thread " + std::to_string(threadId) + " detected broken connection (err=" +
                std::to_string(ctx->err) + "), reconnecting...");
            closeConnection(ctx);
            ctx = createConnection();
            if (!ctx) {
                logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                    "Worker thread " + std::to_string(threadId) + " failed to reconnect");
                return;
            }
            logger->log(Logger::level::INFO, Logger::group::REDIS,
                "Worker thread " + std::to_string(threadId) + " reconnected successfully");

            // Trigger recreation of all clients on reconnection
            recreateAllClients();
        }

        // Execute the task
        try {
            task.operation(ctx);

            // Check if operation caused connection error
            if (ctx->err != 0) {
                logger->log(Logger::level::WARN, Logger::group::REDIS,
                    "Worker thread " + std::to_string(threadId) + " detected connection error after operation (err=" +
                    std::to_string(ctx->err) + "), will reconnect on next task");
            }
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "Worker thread " + std::to_string(threadId) + " caught exception: " + e.what());
        }
    }

    closeConnection(ctx);
    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
        "Worker thread " + std::to_string(threadId) + " stopped");
}

redisContext* RedisSharedState::createConnection() const {
    auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(config.connectionTimeoutMs);

    redisContext* ctx = nullptr;
    int attempts = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        attempts++;

        timeval timeout = {};
        timeout.tv_sec = static_cast<time_t>(config.commandTimeoutMs / 1000);
        timeout.tv_usec = static_cast<suseconds_t>((config.commandTimeoutMs % 1000) * 1000);

        ctx = redisConnectWithTimeout(config.host.c_str(), config.port, timeout);

        if (!ctx) {
            logger->log(Logger::level::WARN, Logger::group::REDIS,
                "Connection attempt " + std::to_string(attempts) + " failed: cannot allocate context");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (ctx->err) {
            logger->log(Logger::level::WARN, Logger::group::REDIS,
                "Connection attempt " + std::to_string(attempts) + " failed: " + std::string(ctx->errstr));
            redisFree(ctx);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Apply SSL if needed
        if (config.useSSL && sslContext) {
            if (redisInitiateSSLWithContext(ctx, sslContext) != REDIS_OK) {
                logger->log(Logger::level::WARN, Logger::group::REDIS,
                    "SSL handshake failed on attempt " + std::to_string(attempts) + ": " + std::string(ctx->errstr));
                redisFree(ctx);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }

        // Authenticate if password is set
        if (!config.password.empty()) {
            auto* reply = static_cast<redisReply*>(redisCommand(ctx, "AUTH %s", config.password.c_str()));
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                logger->log(Logger::level::WARN, Logger::group::REDIS,
                    "Authentication failed on attempt " + std::to_string(attempts));
                if (reply) freeReplyObject(reply);
                redisFree(ctx);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            freeReplyObject(reply);
        }

        // Select database
        if (config.database != 0) {
            auto* reply = static_cast<redisReply*>(redisCommand(ctx, "SELECT %d", config.database));
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                logger->log(Logger::level::WARN, Logger::group::REDIS,
                    "Database selection failed on attempt " + std::to_string(attempts));
                if (reply) freeReplyObject(reply);
                redisFree(ctx);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            freeReplyObject(reply);
        }

        // Register gRPC public facing address in Redis for service discovery
        std::string serviceKey = std::string(REDIS_KEY_PREFIX) + "server:" + std::to_string(serverId);
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "SET %s %s", serviceKey.c_str(), publicFacingRPCAddress.c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            logger->log(Logger::level::WARN, Logger::group::REDIS,
                "Failed to register service key in Redis on attempt " + std::to_string(attempts));
            if (reply) freeReplyObject(reply);
            redisFree(ctx);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        freeReplyObject(reply);

        // Set TTL for service key to 2x client TTL to ensure it persists longer than any client
        uint32_t serviceKeyTTL = config.clientTTLSeconds * 2;
        reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", serviceKey.c_str(), serviceKeyTTL));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            logger->log(Logger::level::WARN, Logger::group::REDIS,
                "Failed to set TTL on service key in Redis on attempt " + std::to_string(attempts));
            if (reply) freeReplyObject(reply);
            redisFree(ctx);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "Connection established on attempt " + std::to_string(attempts));
        return ctx;
    }

    logger->log(Logger::level::FAILURE, Logger::group::REDIS,
        "Failed to connect after " + std::to_string(attempts) + " attempts (timeout)");
    return nullptr;
}

void RedisSharedState::closeConnection(redisContext* ctx) {
    if (ctx) {
        redisFree(ctx);
    }
}

void RedisSharedState::refreshClientTTLs() {
    std::set<uint32_t> pidsToRefresh;
    std::set<uint32_t> splatoonPidsToRefresh;
    std::set<uint32_t> splatoonGatheringsToRefresh;
    {
        std::lock_guard lock(registeredPIDsMutex);
        pidsToRefresh = registeredPIDs;
    }
    {
        std::lock_guard lock(localSplatoonClientCacheMutex);
        for (const auto& [pid, _] : localSplatoonClientCache) {
            (void)_;
            splatoonPidsToRefresh.insert(pid);
        }
    }
    {
        std::lock_guard lock(localSplatoonSessionCacheMutex);
        for (const auto& [gId, _] : localSplatoonSessionCache) {
            (void)_;
            splatoonGatheringsToRefresh.insert(gId);
        }
    }

    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
        "Refreshing TTL for friends clients=" + std::to_string(pidsToRefresh.size()) +
        ", splatoon clients=" + std::to_string(splatoonPidsToRefresh.size()) +
        ", splatoon gatherings=" + std::to_string(splatoonGatheringsToRefresh.size()));

    // Create a task to refresh TTLs
    auto task = std::make_shared<async::ManualTask<void>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr, pidsToRefresh, splatoonPidsToRefresh, splatoonGatheringsToRefresh](redisContext* ctx) {
        // Refresh client TTLs
        for (uint32_t pid : pidsToRefresh) {
            setTTL(ctx, pid);
        }
        for (uint32_t pid : splatoonPidsToRefresh) {
            setSplatoonClientTTL(ctx, pid);
        }
        for (uint32_t gId : splatoonGatheringsToRefresh) {
            setSplatoonSessionTTL(ctx, gId);
        }

        // Refresh server service key TTL
        std::string serviceKey = std::string(REDIS_KEY_PREFIX) + "server:" + std::to_string(serverId);
        uint32_t serviceKeyTTL = config.clientTTLSeconds * 2;
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", serviceKey.c_str(), serviceKeyTTL));
        if (reply) {
            if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1) {
                logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                    "Service key TTL refreshed to " + std::to_string(serviceKeyTTL) + " seconds");
            }
            freeReplyObject(reply);
        }
        const std::string sessionsIndexKey = std::string(REDIS_KEY_PREFIX) + "splatoon:sessions";
        reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", sessionsIndexKey.c_str(), config.clientTTLSeconds));
        if (reply) freeReplyObject(reply);

        taskPtr->complete();
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
}

void RedisSharedState::recreateAllClients() {
    std::map<uint32_t, nex::rmc::FriendsRegisteredClientInfo> clientsToRecreate;
    std::map<uint32_t, nex::rmc::SplatoonRegisteredClientInfo> splatoonClientsToRecreate;
    std::map<uint32_t, nex::rmc::SessionInfo> splatoonSessionsToRecreate;
    {
        std::lock_guard lock(localFriendsClientCacheMutex);
        clientsToRecreate = localFriendsClientCache;
    }
    {
        std::lock_guard lock(localSplatoonClientCacheMutex);
        splatoonClientsToRecreate = localSplatoonClientCache;
    }
    {
        std::lock_guard lock(localSplatoonSessionCacheMutex);
        splatoonSessionsToRecreate = localSplatoonSessionCache;
    }

    if (clientsToRecreate.empty() && splatoonClientsToRecreate.empty() && splatoonSessionsToRecreate.empty()) {
        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "No clients to recreate after reconnection");
        return;
    }

    logger->log(Logger::level::INFO, Logger::group::REDIS,
        "Recreating friendsClients=" + std::to_string(clientsToRecreate.size()) +
        ", splatoonClients=" + std::to_string(splatoonClientsToRecreate.size()) +
        ", splatoonSessions=" + std::to_string(splatoonSessionsToRecreate.size()) + " after reconnection");

    // Create a task to recreate all clients
    Task redisTask;
    redisTask.operation = [this, clientsToRecreate, splatoonClientsToRecreate, splatoonSessionsToRecreate](redisContext* ctx) {
        uint32_t recreatedCount = 0;
        uint32_t refreshedCount = 0;
        const std::string sessionsIndexKey = std::string(REDIS_KEY_PREFIX) + "splatoon:sessions";

        for (const auto& [pid, clientInfo] : clientsToRecreate) {
            std::string clientKey = std::string(REDIS_KEY_PREFIX) + "friends:client:" + std::to_string(pid);
            std::string friendsKey = std::string(REDIS_KEY_PREFIX) + "friends:list:" + std::to_string(pid);

            // Check if the client key still exists
            auto* existsReply = static_cast<redisReply*>(redisCommand(ctx, "EXISTS %s", clientKey.c_str()));
            bool exists = false;
            if (existsReply && existsReply->type == REDIS_REPLY_INTEGER) {
                exists = (existsReply->integer == 1);
            }
            if (existsReply) freeReplyObject(existsReply);

            if (exists) {
                // Key still exists, just refresh TTL
                setTTL(ctx, pid);
                logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                    "Client " + std::to_string(pid) + " still exists, refreshed TTL");
                refreshedCount++;
            } else {
                // Key expired during disconnection, recreate it completely from local cache
                logger->log(Logger::level::INFO, Logger::group::REDIS,
                    "Client " + std::to_string(pid) + " expired during disconnection, recreating from local cache");

                std::string clientInfoStr = serializeClientInfo(clientInfo.client);
                std::string preferenceStr = serializeUserPreference(clientInfo.userData.preference);

                auto* reply = static_cast<redisReply*>(redisCommand(ctx, "HSET %s client %b preference %b",
                    clientKey.c_str(),
                    clientInfoStr.c_str(), clientInfoStr.size(),
                    preferenceStr.c_str(), preferenceStr.size()));
                if (reply) freeReplyObject(reply);

                // Recreate friends list
                reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", friendsKey.c_str()));
                if (reply) freeReplyObject(reply);

                for (uint32_t friendPid : clientInfo.friends) {
                    reply = static_cast<redisReply*>(redisCommand(ctx, "SADD %s %u",
                        friendsKey.c_str(), friendPid));
                    if (reply) freeReplyObject(reply);
                }

                // Set TTL on both keys
                setTTL(ctx, pid);

                recreatedCount++;
            }
        }

        uint32_t recreatedSplatoonClients = 0;
        uint32_t refreshedSplatoonClients = 0;
        uint32_t recreatedSplatoonSessions = 0;
        uint32_t refreshedSplatoonSessions = 0;

        for (const auto& [gId, sessionInfo] : splatoonSessionsToRecreate) {
            if (!sessionInfo.session) {
                continue;
            }

            const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
            auto* existsReply = static_cast<redisReply*>(redisCommand(ctx, "EXISTS %s", sessionKey.c_str()));
            bool exists = existsReply && existsReply->type == REDIS_REPLY_INTEGER && existsReply->integer == 1;
            if (existsReply) freeReplyObject(existsReply);

            if (exists) continue; // Other servers also have control of this, unlike clients, so we shouldn't touch it if it exists

            const auto sessionCopy = *sessionInfo.session;
            const std::string sessionBlob = serializeMatchmakeSession(sessionCopy);
            const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);

            auto* reply = static_cast<redisReply*>(redisCommand(ctx,
                "HSET %s session %b minorVersion %u openParticipation %u participationCount %u progressScore %u",
                sessionKey.c_str(), sessionBlob.c_str(), sessionBlob.size(),
                static_cast<uint32_t>(sessionCopy.minorVersion),
                sessionCopy.openParticipation ? 1U : 0U,
                static_cast<uint32_t>(sessionInfo.players.size()),
                static_cast<uint32_t>(sessionCopy.progressScore)));
            if (reply) freeReplyObject(reply);

            reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", playersKey.c_str()));
            if (reply) freeReplyObject(reply);
            for (uint32_t pid : sessionInfo.players) {
                reply = static_cast<redisReply*>(redisCommand(ctx, "SADD %s %u", playersKey.c_str(), pid));
                if (reply) freeReplyObject(reply);
                reply = static_cast<redisReply*>(redisCommand(ctx, "SADD %s %u",
                    (std::string(REDIS_KEY_PREFIX) + "splatoon:client:gatherings:" + std::to_string(pid)).c_str(), gId));
                if (reply) freeReplyObject(reply);
            }
            reply = static_cast<redisReply*>(redisCommand(ctx, "SADD %s %u", sessionsIndexKey.c_str(), gId));
            if (reply) freeReplyObject(reply);
            setSplatoonSessionTTL(ctx, gId);

            if (!exists) {
                recreatedSplatoonSessions++;
            } else {
                refreshedSplatoonSessions++;
            }
        }

        for (const auto& [pid, clientInfo] : splatoonClientsToRecreate) {
            const std::string clientKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:" + std::to_string(pid);
            const std::string urlsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:urls:" + std::to_string(pid);
            const std::string gatheringsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:gatherings:" + std::to_string(pid);

            auto* existsReply = static_cast<redisReply*>(redisCommand(ctx, "EXISTS %s", clientKey.c_str()));
            bool exists = existsReply && existsReply->type == REDIS_REPLY_INTEGER && existsReply->integer == 1;
            if (existsReply) freeReplyObject(existsReply);

            const std::string serializedClient = serializeClientInfo(clientInfo.client);
            const std::string serializedPublicUrl = serializeStationURL(clientInfo.publicUrl);
            auto* reply = static_cast<redisReply*>(redisCommand(ctx,
                "HSET %s client %b publicUrl %b rvConnId %u natMapping %u natFiltering %u natRtt %u",
                clientKey.c_str(),
                serializedClient.c_str(), serializedClient.size(),
                serializedPublicUrl.c_str(), serializedPublicUrl.size(),
                clientInfo.rvConnId,
                clientInfo.lastReportedNATProperties.mapping,
                clientInfo.lastReportedNATProperties.filtering,
                clientInfo.lastReportedNATProperties.rtt));
            if (reply) freeReplyObject(reply);

            reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", urlsKey.c_str()));
            if (reply) freeReplyObject(reply);
            for (const auto& url : clientInfo.urls) {
                const std::string serializedUrl = serializeStationURL(url);
                reply = static_cast<redisReply*>(redisCommand(ctx, "RPUSH %s %b", urlsKey.c_str(),
                    serializedUrl.c_str(), serializedUrl.size()));
                if (reply) freeReplyObject(reply);
            }

            reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", gatheringsKey.c_str()));
            if (reply) freeReplyObject(reply);
            for (const auto& gathering : clientInfo.joinedGatherings) {
                if (!gathering) {
                    continue;
                }
                reply = static_cast<redisReply*>(redisCommand(ctx, "SADD %s %u", gatheringsKey.c_str(), static_cast<uint32_t>(gathering->id)));
                if (reply) freeReplyObject(reply);
            }

            setSplatoonClientTTL(ctx, pid);
            if (!exists) {
                recreatedSplatoonClients++;
            } else {
                refreshedSplatoonClients++;
            }
        }

        auto* expireReply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", sessionsIndexKey.c_str(), config.clientTTLSeconds));
        if (expireReply) freeReplyObject(expireReply);

        logger->log(Logger::level::INFO, Logger::group::REDIS,
            "Recreation complete: friends=" + std::to_string(recreatedCount) + " recreated, " +
            std::to_string(refreshedCount) + " refreshed; splatoonClients=" +
            std::to_string(recreatedSplatoonClients) + " recreated, " +
            std::to_string(refreshedSplatoonClients) + " refreshed; splatoonSessions=" +
            std::to_string(recreatedSplatoonSessions) + " recreated, " +
            std::to_string(refreshedSplatoonSessions) + " refreshed");
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
}

void RedisSharedState::setTTL(redisContext* ctx, uint32_t pid) {
    std::string clientKey = std::string(REDIS_KEY_PREFIX) + "friends:client:" + std::to_string(pid);
    std::string friendsKey = std::string(REDIS_KEY_PREFIX) + "friends:list:" + std::to_string(pid);

    // Set TTL for both keys
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u",
        clientKey.c_str(), config.clientTTLSeconds));
    if (reply) {
        if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0) {
            // Key doesn't exist, remove from tracked PIDs.
            std::lock_guard lock(registeredPIDsMutex);
            registeredPIDs.erase(pid);
            logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                "PID " + std::to_string(pid) + " key expired, removed from tracking");
        }
        freeReplyObject(reply);
    }

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u",
        friendsKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::setSplatoonClientTTL(redisContext* ctx, uint32_t pid) const {
    const std::string clientKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:" + std::to_string(pid);
    const std::string urlsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:urls:" + std::to_string(pid);
    const std::string gatheringsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:gatherings:" + std::to_string(pid);

    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", clientKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", urlsKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", gatheringsKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::setSplatoonSessionTTL(redisContext* ctx, uint32_t gId) const {
    const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
    const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);

    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", sessionKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", playersKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

// Serialization helpers
std::string RedisSharedState::serializeIPv4Addr(const sock::IPv4Addr& addr) {
    std::ostringstream oss;
    oss << static_cast<int>(addr.a) << "." << static_cast<int>(addr.b) << "."
        << static_cast<int>(addr.c) << "." << static_cast<int>(addr.d) << ":" << addr.port;
    return oss.str();
}

sock::IPv4Addr RedisSharedState::deserializeIPv4Addr(const std::string& str) {
    sock::IPv4Addr addr{};
    char dot1, dot2, dot3, colon;
    int a, b, c, d;
    std::istringstream iss(str);
    iss >> a >> dot1 >> b >> dot2 >> c >> dot3 >> d >> colon >> addr.port;
    addr.a = static_cast<uint8_t>(a);
    addr.b = static_cast<uint8_t>(b);
    addr.c = static_cast<uint8_t>(c);
    addr.d = static_cast<uint8_t>(d);
    return addr;
}

std::string RedisSharedState::serializePRUDPAddress(const nex::prudp::PRUDPAddress& addr) {
    std::ostringstream oss;
    oss << serializeIPv4Addr(addr.address) << "|"
        << static_cast<int>(addr.vPort) << "|"
        << static_cast<int>(addr.streamType) << "|"
        << static_cast<int>(addr.srcVPort) << "|"
        << static_cast<int>(addr.srcStreamType);
    return oss.str();
}

nex::prudp::PRUDPAddress RedisSharedState::deserializePRUDPAddress(const std::string& str) {
    nex::prudp::PRUDPAddress addr{};
    size_t pos1 = str.find('|');
    if (pos1 == std::string::npos) return addr;

    addr.address = deserializeIPv4Addr(str.substr(0, pos1));

    size_t pos2 = str.find('|', pos1 + 1);
    if (pos2 == std::string::npos) return addr;
    addr.vPort = static_cast<uint8_t>(std::stoi(str.substr(pos1 + 1, pos2 - pos1 - 1)));

    size_t pos3 = str.find('|', pos2 + 1);
    if (pos3 == std::string::npos) return addr;
    addr.streamType = static_cast<uint8_t>(std::stoi(str.substr(pos2 + 1, pos3 - pos2 - 1)));

    size_t pos4 = str.find('|', pos3 + 1);
    if (pos4 == std::string::npos) return addr;
    addr.srcVPort = static_cast<uint8_t>(std::stoi(str.substr(pos3 + 1, pos4 - pos3 - 1)));

    addr.srcStreamType = static_cast<uint8_t>(std::stoi(str.substr(pos4 + 1)));

    return addr;
}

std::string RedisSharedState::serializeClientInfo(const nex::rmc::ClientInfo& info) {
    std::ostringstream oss;
    oss << serializePRUDPAddress(info.address) << "|"
        << static_cast<int>(info.minorVersion) << "|"
        << static_cast<int>(info.substreamId) << "|"
        << info.serverId << "|"
        << info.pid;
    return oss.str();
}

nex::rmc::ClientInfo RedisSharedState::deserializeClientInfo(const std::string& str) {
    nex::rmc::ClientInfo info{};

    // Find the last three pipes (for minorVersion, substreamId, serverId, pid)
    const size_t lastPipe = str.rfind('|');
    if (lastPipe == std::string::npos) return info;
    info.pid = std::stoul(str.substr(lastPipe + 1));

    const size_t secondLastPipe = str.rfind('|', lastPipe - 1);
    if (secondLastPipe == std::string::npos) return info;
    info.serverId = std::stoul(str.substr(secondLastPipe + 1, lastPipe - secondLastPipe - 1));

    const size_t thirdLastPipe = str.rfind('|', secondLastPipe - 1);
    if (thirdLastPipe == std::string::npos) return info;
    info.substreamId = static_cast<uint8_t>(std::stoi(str.substr(thirdLastPipe + 1, secondLastPipe - thirdLastPipe - 1)));

    const size_t fourthLastPipe = str.rfind('|', thirdLastPipe - 1);
    if (fourthLastPipe == std::string::npos) return info;
    info.minorVersion = static_cast<uint8_t>(std::stoi(str.substr(fourthLastPipe + 1, thirdLastPipe - fourthLastPipe - 1)));

    info.address = deserializePRUDPAddress(str.substr(0, fourthLastPipe));

    return info;
}

std::string RedisSharedState::serializeUserPreference(const nex::rmc::UserPreference& pref) {
    std::ostringstream oss;
    oss << (pref.showOnline ? "1" : "0") << "|"
        << (pref.showPlaying ? "1" : "0") << "|"
        << (pref.blockFriendRequest ? "1" : "0");
    return oss.str();
}

nex::rmc::UserPreference RedisSharedState::deserializeUserPreference(const std::string& str) {
    nex::rmc::UserPreference pref{};
    size_t pos1 = str.find('|');
    if (pos1 == std::string::npos) return pref;

    pref.showOnline = (str[0] == '1');

    size_t pos2 = str.find('|', pos1 + 1);
    if (pos2 == std::string::npos) return pref;
    pref.showPlaying = (str[pos1 + 1] == '1');

    pref.blockFriendRequest = (str[pos2 + 1] == '1');

    return pref;
}

std::string RedisSharedState::serializeStationURL(const nex::rmc::StationURL& url) {
    const std::vector<uint8_t> encoded = url.encode();
    return {encoded.begin(), encoded.end()};
}

nex::rmc::StationURL RedisSharedState::deserializeStationURL(const std::string& str, uint8_t minorVersion) {
    nex::rmc::StationURL url(minorVersion);
    std::vector<uint8_t> data(str.begin(), str.end());
    url.decode(data);
    return url;
}

std::string RedisSharedState::serializeMatchmakeSession(const nex::rmc::MatchmakeSession& session) {
    const std::vector<uint8_t> encoded = session.encode();
    return {encoded.begin(), encoded.end()};
}

nex::rmc::MatchmakeSession RedisSharedState::deserializeMatchmakeSession(const std::string& str, uint8_t minorVersion) {
    nex::rmc::MatchmakeSession session(minorVersion);
    std::vector<uint8_t> data(str.begin(), str.end());
    session.decode(data);
    return session;
}

// Implementation of SharedState interface methods

async::ManualTask<std::pair<Result, std::optional<std::string>>> RedisSharedState::getPublicFacingRPCAddress(uint32_t serverId) {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<std::string>>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr, serverId](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getPublicFacingRPCAddress",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt)); return);

        std::string serviceKey = std::string(REDIS_KEY_PREFIX) + "server:" + std::to_string(serverId);
        const auto* reply = static_cast<redisReply*>(redisCommand(ctx, "GET %s", serviceKey.c_str()));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        if (reply->type == REDIS_REPLY_STRING) {
            std::string address(reply->str, reply->len);
            taskPtr->complete(std::make_pair(Result::SUCCESS, address));
        } else {
            taskPtr->complete(std::make_pair(Result::SUCCESS, std::nullopt));
        }
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::setFriendsRegisteredClientInfo(
    const nex::rmc::FriendsRegisteredClientInfo&& info) {

    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    const uint32_t pid = info.userData.pid;
    std::string clientInfoStr = serializeClientInfo(info.client);
    std::string preferenceStr = serializeUserPreference(info.userData.preference);
    std::vector<uint32_t> friends(info.friends.begin(), info.friends.end());

    // Register this PID for TTL tracking
    {
        std::lock_guard lock(registeredPIDsMutex);
        registeredPIDs.insert(pid);
    }

    // Store in local cache for recreation after reconnection
    {
        std::lock_guard lock(localFriendsClientCacheMutex);
        localFriendsClientCache[pid] = info;
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, clientInfoStr, preferenceStr, friends](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "setFriendsRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        std::string key = std::string(REDIS_KEY_PREFIX) + "friends:client:" + std::to_string(pid);
        std::string friendsKey = std::string(REDIS_KEY_PREFIX) + "friends:list:" + std::to_string(pid);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "setFriendsRegisteredClientInfo for PID " + std::to_string(pid));

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "HSET %s client %b preference %b",
            key.c_str(),
            clientInfoStr.c_str(), clientInfoStr.size(),
            preferenceStr.c_str(), preferenceStr.size()));
        if (reply) freeReplyObject(reply);

        // Set friends list
        reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", friendsKey.c_str()));
        if (reply) freeReplyObject(reply);

        for (uint32_t friendPid : friends) {
            reply = static_cast<redisReply*>(redisCommand(ctx, "SADD %s %u",
                friendsKey.c_str(), friendPid));
            if (reply) freeReplyObject(reply);
        }

        // Set TTL on both keys
        setTTL(ctx, pid);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "setFriendsRegisteredClientInfo completed for PID " + std::to_string(pid));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<std::pair<Result, std::optional<nex::rmc::FriendsRegisteredClientInfo>>>
RedisSharedState::getFriendsRegisteredClientInfo(uint32_t pid) {

    auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<nex::rmc::FriendsRegisteredClientInfo>>>>();
    {
        std::lock_guard lock(localFriendsClientCacheMutex);
        if (localFriendsClientCache.contains(pid)) {
            logger->log(Logger::level::DEBUG, Logger::group::REDIS, "getFriendsRegisteredClientInfo cache hit for PID " + std::to_string(pid));
            task->complete(std::make_pair(Result::SUCCESS, localFriendsClientCache[pid]));
            return *task;
        }
    }

    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getFriendsRegisteredClientInfo",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt)); return);

        std::string key = std::string(REDIS_KEY_PREFIX) + "friends:client:" + std::to_string(pid);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getFriendsRegisteredClientInfo for PID " + std::to_string(pid));

        // Get client info and preference
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "HGETALL %s", key.c_str()));
        if (!reply) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "HGETALL failed for getFriendsRegisteredClientInfo");
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 0) {
            // Key doesn't exist
            freeReplyObject(reply);
            logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                "Client info not found for PID " + std::to_string(pid));
            taskPtr->complete(std::make_pair(Result::SUCCESS, std::nullopt));
            return;
        }

        if (reply->type != REDIS_REPLY_ARRAY || reply->elements % 2 != 0) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "Invalid HGETALL response for getFriendsRegisteredClientInfo");
            freeReplyObject(reply);
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        std::string clientInfoStr;
        std::string preferenceStr;

        for (size_t i = 0; i < reply->elements; i += 2) {
            std::string field(reply->element[i]->str, reply->element[i]->len);
            std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);

            if (field == "client") {
                clientInfoStr = value;
            } else if (field == "preference") {
                preferenceStr = value;
            }
        }
        freeReplyObject(reply);

        if (clientInfoStr.empty() || preferenceStr.empty()) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "Incomplete client info for PID " + std::to_string(pid));
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        // Get friends list
        reply = static_cast<redisReply*>(redisCommand(ctx, "SMEMBERS %s",
            (std::string(REDIS_KEY_PREFIX) + "friends:list:" + std::to_string(pid)).c_str()));
        if (!reply) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "SMEMBERS failed for getFriendsRegisteredClientInfo");
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        std::set<uint32_t> friends;
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                friends.insert(std::stoul(reply->element[i]->str));
            }
        }
        freeReplyObject(reply);

        // Deserialize
        nex::rmc::FriendsRegisteredClientInfo info;
        info.client = deserializeClientInfo(clientInfoStr);
        info.userData.pid = pid;
        info.userData.preference = deserializeUserPreference(preferenceStr);
        info.friends = friends;

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getFriendsRegisteredClientInfo completed for PID " + std::to_string(pid));
        taskPtr->complete(std::make_pair(Result::SUCCESS, info));
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::deleteFriendsRegisteredClientInfo(uint32_t pid) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    // Unregister this PID from TTL tracking
    {
        std::lock_guard lock(registeredPIDsMutex);
        registeredPIDs.erase(pid);
    }

    // Remove from local cache
    {
        std::lock_guard lock(localFriendsClientCacheMutex);
        localFriendsClientCache.erase(pid);
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "deleteFriendsRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "deleteFriendsRegisteredClientInfo for PID " + std::to_string(pid));

        std::string key = std::string(REDIS_KEY_PREFIX) + "friends:client:" + std::to_string(pid);
        std::string friendsKey = std::string(REDIS_KEY_PREFIX) + "friends:list:" + std::to_string(pid);

        // Use MULTI/EXEC for atomicity
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "MULTI"));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "MULTI failed for deleteFriendsRegisteredClientInfo");
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", key.c_str()));
        if (reply) freeReplyObject(reply);

        reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", friendsKey.c_str()));
        if (reply) freeReplyObject(reply);

        reply = static_cast<redisReply*>(redisCommand(ctx, "EXEC"));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "EXEC failed for deleteFriendsRegisteredClientInfo");
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "deleteFriendsRegisteredClientInfo completed for PID " + std::to_string(pid));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::addFriendToRegisteredClientInfo(uint32_t pid, uint32_t friendPid) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    // Update local cache
    {
        std::lock_guard lock(localFriendsClientCacheMutex);
        auto it = localFriendsClientCache.find(pid);
        if (it != localFriendsClientCache.end()) {
            it->second.friends.insert(friendPid);
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, friendPid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "addFriendToRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "addFriendToRegisteredClientInfo: PID " + std::to_string(pid) + " adding friend " + std::to_string(friendPid));

        std::string friendsKey = std::string(REDIS_KEY_PREFIX) + "friends:list:" + std::to_string(pid);

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "SADD %s %u",
            friendsKey.c_str(), friendPid));

        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "SADD failed for addFriendToRegisteredClientInfo");
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "addFriendToRegisteredClientInfo completed");
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::removeFriendFromRegisteredClientInfo(uint32_t pid, uint32_t friendPid) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    // Update local cache
    {
        std::lock_guard lock(localFriendsClientCacheMutex);
        auto it = localFriendsClientCache.find(pid);
        if (it != localFriendsClientCache.end()) {
            it->second.friends.erase(friendPid);
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, friendPid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "removeFriendFromRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "removeFriendFromRegisteredClientInfo: PID " + std::to_string(pid) + " removing friend " + std::to_string(friendPid));

        std::string friendsKey = std::string(REDIS_KEY_PREFIX) + "friends:list:" + std::to_string(pid);

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "SREM %s %u",
            friendsKey.c_str(), friendPid));

        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "SREM failed for removeFriendFromRegisteredClientInfo");
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "removeFriendFromRegisteredClientInfo completed");
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::updatePreferenceInRegisteredFriendsClientInfo(
    uint32_t pid, const nex::rmc::UserPreference& preference) {

    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    std::string preferenceStr = serializeUserPreference(preference);

    // Update local cache
    {
        std::lock_guard lock(localFriendsClientCacheMutex);
        auto it = localFriendsClientCache.find(pid);
        if (it != localFriendsClientCache.end()) {
            it->second.userData.preference = preference;
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, preferenceStr](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "updatePreferenceInRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "updatePreferenceInRegisteredClientInfo for PID " + std::to_string(pid));

        std::string key = std::string(REDIS_KEY_PREFIX) + "friends:client:" + std::to_string(pid);

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "HSET %s preference %b",
            key.c_str(), preferenceStr.c_str(), preferenceStr.size()));

        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "HSET failed for updatePreferenceInRegisteredClientInfo");
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "updatePreferenceInRegisteredClientInfo completed for PID " + std::to_string(pid));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<std::pair<Result, uint32_t>> RedisSharedState::getFriendsRegisteredClientCount() {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, uint32_t>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getFriendsRegisteredClientCount",
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL)); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getFriendsRegisteredClientCount");

        uint32_t count = 0;
        if (!countKeysByPattern(ctx, std::string(REDIS_KEY_PREFIX) + "friends:client:*", count)) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "SCAN count failed for getFriendsRegisteredClientCount");
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL));
            return;
        }

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getFriendsRegisteredClientCount completed, count: " + std::to_string(count));
        taskPtr->complete(std::make_pair(Result::SUCCESS, count));
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::setSplatoonRegisteredClientInfo(
    const nex::rmc::SplatoonRegisteredClientInfo&& info) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    const uint32_t pid = info.client.pid;
    const std::string clientInfoStr = serializeClientInfo(info.client);
    const std::string publicUrlStr = serializeStationURL(info.publicUrl);
    const uint32_t rvConnId = info.rvConnId;
    const uint32_t natMapping = info.lastReportedNATProperties.mapping;
    const uint32_t natFiltering = info.lastReportedNATProperties.filtering;
    const uint32_t natRtt = info.lastReportedNATProperties.rtt;

    std::vector<std::string> encodedUrls;
    encodedUrls.reserve(info.urls.size());
    for (const auto& url : info.urls) {
        encodedUrls.emplace_back(serializeStationURL(url));
    }

    std::vector<uint32_t> gatheringIds;
    gatheringIds.reserve(info.joinedGatherings.size());
    for (const auto& gathering : info.joinedGatherings) {
        if (gathering) {
            gatheringIds.push_back(gathering->id);
        }
    }

    {
        std::lock_guard lock(localSplatoonClientCacheMutex);
        localSplatoonClientCache[pid] = info;
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, clientInfoStr, publicUrlStr, rvConnId, natMapping, natFiltering, natRtt, encodedUrls, gatheringIds](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "setSplatoonRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "setSplatoonRegisteredClientInfo for PID " + std::to_string(pid) +
            " (urls=" + std::to_string(encodedUrls.size()) +
            ", gatherings=" + std::to_string(gatheringIds.size()) + ")");

        const std::string clientKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:urls:" + std::to_string(pid);
        const std::string gatheringsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:gatherings:" + std::to_string(pid);

        const std::string luaScript = R"LUASCRIPT(
local clientKey=KEYS[1]
local urlsKey=KEYS[2]
local gatheringsKey=KEYS[3]
redis.call('HSET', clientKey,
    'client', ARGV[1],
    'publicUrl', ARGV[2],
    'rvConnId', ARGV[3],
    'natMapping', ARGV[4],
    'natFiltering', ARGV[5],
    'natRtt', ARGV[6])
local ttl=tonumber(ARGV[7])
local urlCount=tonumber(ARGV[8])
local idx=9
redis.call('DEL', urlsKey)
for i=1,urlCount do
    redis.call('RPUSH', urlsKey, ARGV[idx])
    idx = idx + 1
end
local gatheringCount=tonumber(ARGV[idx])
idx = idx + 1
redis.call('DEL', gatheringsKey)
for i=1,gatheringCount do
    redis.call('SADD', gatheringsKey, ARGV[idx])
    idx = idx + 1
end
redis.call('EXPIRE', clientKey, ttl)
redis.call('EXPIRE', urlsKey, ttl)
redis.call('EXPIRE', gatheringsKey, ttl)
return 1
)LUASCRIPT";

        std::vector<std::string> args;
        args.reserve(13 + encodedUrls.size() + gatheringIds.size());
        args.emplace_back("EVAL");
        args.push_back(luaScript);
        args.emplace_back("3");
        args.push_back(clientKey);
        args.push_back(urlsKey);
        args.push_back(gatheringsKey);
        args.push_back(clientInfoStr);
        args.push_back(publicUrlStr);
        args.push_back(std::to_string(rvConnId));
        args.push_back(std::to_string(natMapping));
        args.push_back(std::to_string(natFiltering));
        args.push_back(std::to_string(natRtt));
        args.push_back(std::to_string(config.clientTTLSeconds));
        args.push_back(std::to_string(encodedUrls.size()));
        for (const auto& encodedUrl : encodedUrls) {
            args.push_back(encodedUrl);
        }
        args.push_back(std::to_string(gatheringIds.size()));
        for (uint32_t gId : gatheringIds) {
            args.push_back(std::to_string(gId));
        }

        std::vector<const char*> argv;
        std::vector<size_t> argvlen;
        argv.reserve(args.size());
        argvlen.reserve(args.size());
        for (const auto& arg : args) {
            argv.push_back(arg.data());
            argvlen.push_back(arg.size());
        }

        auto* reply = static_cast<redisReply*>(redisCommandArgv(ctx, static_cast<int>(argv.size()), argv.data(), argvlen.data()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "setSplatoonRegisteredClientInfo completed for PID " + std::to_string(pid));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<std::pair<Result, std::optional<nex::rmc::SplatoonRegisteredClientInfo>>>
RedisSharedState::getSplatoonRegisteredClientInfo(uint32_t pid) {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<nex::rmc::SplatoonRegisteredClientInfo>>>>();
    const auto& taskPtr = task;

    {
        std::lock_guard lock(localSplatoonClientCacheMutex);
        if (localSplatoonClientCache.contains(pid)) {
            logger->log(Logger::level::DEBUG, Logger::group::REDIS, "getSplatoonRegisteredClientInfo cache hit for PID " + std::to_string(pid));
            task->complete(std::make_pair(Result::SUCCESS, localSplatoonClientCache[pid]));
            return *task;
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getSplatoonRegisteredClientInfo",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt)); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonRegisteredClientInfo for PID " + std::to_string(pid));

        const std::string clientKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:urls:" + std::to_string(pid);
        const std::string gatheringsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:gatherings:" + std::to_string(pid);

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "HGETALL %s", clientKey.c_str()));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 0) {
            freeReplyObject(reply);
            logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                "getSplatoonRegisteredClientInfo not found for PID " + std::to_string(pid));
            taskPtr->complete(std::make_pair(Result::SUCCESS, std::nullopt));
            return;
        }

        if (reply->type != REDIS_REPLY_ARRAY || reply->elements % 2 != 0) {
            freeReplyObject(reply);
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        std::string clientInfoStr;
        std::string publicUrlStr;
        uint32_t rvConnId = 0;
        nex::rmc::NATProperties natProperties{};

        for (size_t i = 0; i < reply->elements; i += 2) {
            const std::string field(reply->element[i]->str, reply->element[i]->len);
            const std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);

            if (field == "client") {
                clientInfoStr = value;
            } else if (field == "publicUrl") {
                publicUrlStr = value;
            } else if (field == "rvConnId") {
                rvConnId = static_cast<uint32_t>(std::stoul(value));
            } else if (field == "natMapping") {
                natProperties.mapping = static_cast<uint32_t>(std::stoul(value));
            } else if (field == "natFiltering") {
                natProperties.filtering = static_cast<uint32_t>(std::stoul(value));
            } else if (field == "natRtt") {
                natProperties.rtt = static_cast<uint32_t>(std::stoul(value));
            }
        }
        freeReplyObject(reply);

        if (clientInfoStr.empty()) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        nex::rmc::SplatoonRegisteredClientInfo info;
        info.client = deserializeClientInfo(clientInfoStr);
        info.rvConnId = rvConnId;
        info.lastReportedNATProperties = natProperties;

        if (!publicUrlStr.empty()) {
            info.publicUrl = deserializeStationURL(publicUrlStr, info.client.minorVersion);
        }

        reply = static_cast<redisReply*>(redisCommand(ctx, "LRANGE %s 0 -1", urlsKey.c_str()));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                info.urls.push_back(deserializeStationURL(
                    std::string(reply->element[i]->str, reply->element[i]->len),
                    info.client.minorVersion));
            }
        }
        freeReplyObject(reply);

        reply = static_cast<redisReply*>(redisCommand(ctx, "SMEMBERS %s", gatheringsKey.c_str()));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        std::unordered_map<uint32_t, std::shared_ptr<nex::rmc::MatchmakeSession>> sessionPtrs;
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                const uint32_t gId = static_cast<uint32_t>(std::stoul(reply->element[i]->str));
                const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);

                auto* sessionReply = static_cast<redisReply*>(redisCommand(ctx, "HGETALL %s", sessionKey.c_str()));
                if (!sessionReply) {
                    continue;
                }

                std::string sessionBlob;
                uint8_t minorVersion = info.client.minorVersion;
                uint32_t openParticipation = 0;
                uint32_t participationCount = 0;
                uint32_t progressScore = 0;

                if (sessionReply->type == REDIS_REPLY_ARRAY && sessionReply->elements % 2 == 0) {
                    for (size_t j = 0; j < sessionReply->elements; j += 2) {
                        const std::string field(sessionReply->element[j]->str, sessionReply->element[j]->len);
                        const std::string value(sessionReply->element[j + 1]->str, sessionReply->element[j + 1]->len);
                        if (field == "session") sessionBlob = value;
                        else if (field == "minorVersion") minorVersion = static_cast<uint8_t>(std::stoul(value));
                        else if (field == "openParticipation") openParticipation = static_cast<uint32_t>(std::stoul(value));
                        else if (field == "participationCount") participationCount = static_cast<uint32_t>(std::stoul(value));
                        else if (field == "progressScore") progressScore = static_cast<uint32_t>(std::stoul(value));
                    }
                }
                freeReplyObject(sessionReply);

                if (sessionBlob.empty()) {
                    continue;
                }

                auto session = std::make_shared<nex::rmc::MatchmakeSession>(deserializeMatchmakeSession(sessionBlob, minorVersion));
                session->openParticipation = openParticipation != 0;
                session->participationCount = participationCount;
                session->progressScore = static_cast<uint8_t>(progressScore);
                sessionPtrs[gId] = session;
            }
        }
        freeReplyObject(reply);

        info.joinedGatherings.reserve(sessionPtrs.size());
        for (const auto& [gId, session] : sessionPtrs) {
            (void)gId;
            info.joinedGatherings.push_back(session);
        }

        {
            std::lock_guard lock(localSplatoonClientCacheMutex);
            localSplatoonClientCache[pid] = info;
        }

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonRegisteredClientInfo completed for PID " + std::to_string(pid));
        taskPtr->complete(std::make_pair(Result::SUCCESS, info));
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::deleteSplatoonRegisteredClientInfo(uint32_t pid) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    {
        std::scoped_lock lock(localSplatoonClientCacheMutex, localSplatoonSessionCacheMutex);
        localSplatoonClientCache.erase(pid);
        for (auto& [_, sessionInfo] : localSplatoonSessionCache) {
            sessionInfo.players.erase(pid);
            if (sessionInfo.session) {
                sessionInfo.session->participationCount = static_cast<uint32_t>(sessionInfo.players.size());
            }
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "deleteSplatoonRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "deleteSplatoonRegisteredClientInfo for PID " + std::to_string(pid));

        const std::string clientKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:urls:" + std::to_string(pid);
        const std::string gatheringsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:gatherings:" + std::to_string(pid);

        const char* luaScript = R"LUASCRIPT(
local clientKey = KEYS[1]
local urlsKey = KEYS[2]
local gatheringsKey = KEYS[3]
local pid = ARGV[1]
local keyPrefix = ARGV[3]
local gatherings = redis.call('SMEMBERS', gatheringsKey)
for _,gid in ipairs(gatherings) do
    local playersKey = keyPrefix .. 'splatoon:session:players:' .. gid
    local sessionKey = keyPrefix .. 'splatoon:session:' .. gid
    redis.call('SREM', playersKey, pid)
    local count = redis.call('SCARD', playersKey)
    redis.call('HSET', sessionKey, 'participationCount', count)
    redis.call('EXPIRE', playersKey, tonumber(ARGV[2]))
    redis.call('EXPIRE', sessionKey, tonumber(ARGV[2]))
end
redis.call('DEL', clientKey)
redis.call('DEL', urlsKey)
redis.call('DEL', gatheringsKey)
return 1
)LUASCRIPT";

        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx, "EVAL %s 3 %s %s %s %u %u %s",
                luaScript, clientKey.c_str(), urlsKey.c_str(), gatheringsKey.c_str(), pid, config.clientTTLSeconds,
                std::string(REDIS_KEY_PREFIX).c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "deleteSplatoonRegisteredClientInfo completed for PID " + std::to_string(pid));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::updateSplatoonRegisteredClientURLs(
    uint32_t pid, const std::vector<nex::rmc::StationURL>& urls) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    std::vector<std::string> encodedUrls;
    encodedUrls.reserve(urls.size());
    for (const auto& url : urls) {
        encodedUrls.emplace_back(serializeStationURL(url));
    }

    {
        std::lock_guard lock(localSplatoonClientCacheMutex);
        auto it = localSplatoonClientCache.find(pid);
        if (it != localSplatoonClientCache.end()) {
            it->second.urls = urls;
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, encodedUrls](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "updateSplatoonRegisteredClientURLs", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "updateSplatoonRegisteredClientURLs for PID " + std::to_string(pid) +
            " (urls=" + std::to_string(encodedUrls.size()) + ")");

        const std::string clientKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:urls:" + std::to_string(pid);

        auto* existsReply = static_cast<redisReply*>(redisCommand(ctx, "EXISTS %s", clientKey.c_str()));
        if (!existsReply || existsReply->type != REDIS_REPLY_INTEGER || existsReply->integer == 0) {
            if (existsReply) freeReplyObject(existsReply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(existsReply);

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "MULTI"));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", urlsKey.c_str()));
        if (reply) freeReplyObject(reply);

        for (const auto& encodedUrl : encodedUrls) {
            reply = static_cast<redisReply*>(redisCommand(ctx, "RPUSH %s %b", urlsKey.c_str(), encodedUrl.c_str(), encodedUrl.size()));
            if (reply) freeReplyObject(reply);
        }

        reply = static_cast<redisReply*>(redisCommand(ctx, "EXEC"));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        setSplatoonClientTTL(ctx, pid);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "updateSplatoonRegisteredClientURLs completed for PID " + std::to_string(pid));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<Result> RedisSharedState::updateSplatoonRegisteredClientLastReportedNATProperties(
    uint32_t pid, const nex::rmc::NATProperties& natProperties) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    {
        std::lock_guard lock(localSplatoonClientCacheMutex);
        auto it = localSplatoonClientCache.find(pid);
        if (it != localSplatoonClientCache.end()) {
            it->second.lastReportedNATProperties = natProperties;
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, natProperties](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "updateSplatoonRegisteredClientLastReportedNATProperties", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "updateSplatoonRegisteredClientLastReportedNATProperties for PID " + std::to_string(pid));

        const std::string clientKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:" + std::to_string(pid);
        auto* reply = static_cast<redisReply*>(redisCommand(ctx,
            "HSET %s natMapping %u natFiltering %u natRtt %u",
            clientKey.c_str(), natProperties.mapping, natProperties.filtering, natProperties.rtt));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        setSplatoonClientTTL(ctx, pid);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "updateSplatoonRegisteredClientLastReportedNATProperties completed for PID " + std::to_string(pid));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<std::pair<Result, uint32_t>> RedisSharedState::getSplatoonRegisteredClientCount() {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, uint32_t>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getSplatoonRegisteredClientCount",
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL)); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonRegisteredClientCount");

        uint32_t count = 0;
        // Use wildcard pattern but exclude keys with additional colons after "client:"
        // This excludes splatoon:client:urls:* and splatoon:client:gatherings:*
        // Pattern matches keys with 4+ colons (prefix:splatoon:client:extra:id)
        if (!countKeysByPattern(ctx, std::string(REDIS_KEY_PREFIX) + "splatoon:client:*", count, ":.*:.*:.*:")) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "SCAN count failed for getSplatoonRegisteredClientCount");
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL));
            return;
        }

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonRegisteredClientCount completed, count: " + std::to_string(count));
        taskPtr->complete(std::make_pair(Result::SUCCESS, count));
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<Result> RedisSharedState::setSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    if (!sessionInfo.session) {
        task->complete(Result::FAILURE);
        return *task;
    }

    const uint32_t gId = sessionInfo.session->id;
    nex::rmc::MatchmakeSession sessionCopy = *sessionInfo.session;
    sessionCopy.participationCount = static_cast<uint32_t>(sessionInfo.players.size());
    const std::string sessionBlob = serializeMatchmakeSession(sessionCopy);
    const uint8_t minorVersion = sessionCopy.minorVersion;
    const uint32_t openParticipation = sessionCopy.openParticipation ? 1U : 0U;
    const uint32_t participationCount = sessionCopy.participationCount;
    const uint32_t progressScore = sessionCopy.progressScore;
    std::vector<uint32_t> players(sessionInfo.players.begin(), sessionInfo.players.end());

    {
        std::scoped_lock lock(localSplatoonClientCacheMutex, localSplatoonSessionCacheMutex);
        auto sessionPtr = std::make_shared<nex::rmc::MatchmakeSession>(sessionCopy);
        localSplatoonSessionCache[gId] = {sessionPtr, sessionInfo.players};
        for (auto& [pid, clientInfo] : localSplatoonClientCache) {
            auto gatheringIt = std::ranges::find_if(clientInfo.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
                return gathering && gathering->id == gId;
            });

            const bool shouldContain = sessionInfo.players.contains(pid);
            if (shouldContain && gatheringIt == clientInfo.joinedGatherings.end()) {
                clientInfo.joinedGatherings.push_back(sessionPtr);
            } else if (!shouldContain && gatheringIt != clientInfo.joinedGatherings.end()) {
                clientInfo.joinedGatherings.erase(gatheringIt);
            } else if (shouldContain && gatheringIt != clientInfo.joinedGatherings.end()) {
                *gatheringIt = sessionPtr;
            }
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, gId, sessionBlob, minorVersion, openParticipation, participationCount, progressScore, players](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "setSplatoonMatchmakeSession", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "setSplatoonMatchmakeSession for GID " + std::to_string(gId) +
            " (players=" + std::to_string(players.size()) + ")");

        const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
        const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);
        const std::string sessionsIndexKey = std::string(REDIS_KEY_PREFIX) + "splatoon:sessions";
        const std::string luaScript = R"LUASCRIPT(
local sessionKey=KEYS[1]
local playersKey=KEYS[2]
local sessionsIndex=KEYS[3]
local gid=ARGV[1]
local ttl=tonumber(ARGV[7])
local keyPrefix=ARGV[8]
local oldPlayers=redis.call('SMEMBERS', playersKey)
for _,pid in ipairs(oldPlayers) do
    redis.call('SREM', keyPrefix .. 'splatoon:client:gatherings:' .. pid, gid)
    redis.call('EXPIRE', keyPrefix .. 'splatoon:client:gatherings:' .. pid, ttl)
end
redis.call('DEL', playersKey)
for i=9,#ARGV do
    local pid=ARGV[i]
    redis.call('SADD', playersKey, pid)
    redis.call('SADD', keyPrefix .. 'splatoon:client:gatherings:' .. pid, gid)
    redis.call('EXPIRE', keyPrefix .. 'splatoon:client:gatherings:' .. pid, ttl)
end
redis.call('HSET', sessionKey,
    'session', ARGV[2],
    'minorVersion', ARGV[3],
    'openParticipation', ARGV[4],
    'participationCount', ARGV[5],
    'progressScore', ARGV[6])
redis.call('SADD', sessionsIndex, gid)
redis.call('EXPIRE', sessionKey, ttl)
redis.call('EXPIRE', playersKey, ttl)
redis.call('EXPIRE', sessionsIndex, ttl)
return 1
)LUASCRIPT";

        std::vector<std::string> args;
        args.reserve(14 + players.size());
        args.emplace_back("EVAL");
        args.push_back(luaScript);
        args.emplace_back("3");
        args.push_back(sessionKey);
        args.push_back(playersKey);
        args.push_back(sessionsIndexKey);
        args.push_back(std::to_string(gId));
        args.push_back(sessionBlob);
        args.push_back(std::to_string(static_cast<uint32_t>(minorVersion)));
        args.push_back(std::to_string(openParticipation ? 1U : 0U));
        args.push_back(std::to_string(participationCount));
        args.push_back(std::to_string(progressScore));
        args.push_back(std::to_string(config.clientTTLSeconds));
        args.emplace_back(REDIS_KEY_PREFIX);
        for (uint32_t pid : players) {
            args.push_back(std::to_string(pid));
        }

        std::vector<const char*> argv;
        std::vector<size_t> argvlen;
        argv.reserve(args.size());
        argvlen.reserve(args.size());
        for (auto& arg : args) {
            argv.push_back(arg.c_str());
            argvlen.push_back(arg.size());
        }

        auto* reply = static_cast<redisReply*>(redisCommandArgv(ctx, static_cast<int>(argv.size()), argv.data(), argvlen.data()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "setSplatoonMatchmakeSession completed for GID " + std::to_string(gId));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<std::pair<Result, std::optional<nex::rmc::SessionInfo>>>
RedisSharedState::getSplatoonMatchmakeSession(uint32_t gId) {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<nex::rmc::SessionInfo>>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr, gId](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getSplatoonMatchmakeSession",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt)); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonMatchmakeSession for GID " + std::to_string(gId));

        const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
        const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "HGETALL %s", sessionKey.c_str()));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 0) {
            freeReplyObject(reply);
            logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                "getSplatoonMatchmakeSession not found for GID " + std::to_string(gId));
            taskPtr->complete(std::make_pair(Result::SUCCESS, std::nullopt));
            return;
        }

        if (reply->type != REDIS_REPLY_ARRAY || reply->elements % 2 != 0) {
            freeReplyObject(reply);
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        std::string sessionBlob;
        uint8_t minorVersion = 0;
        uint32_t openParticipation = 0;
        uint32_t participationCount = 0;
        uint32_t progressScore = 0;

        for (size_t i = 0; i < reply->elements; i += 2) {
            const std::string field(reply->element[i]->str, reply->element[i]->len);
            const std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);
            if (field == "session") sessionBlob = value;
            else if (field == "minorVersion") minorVersion = static_cast<uint8_t>(std::stoul(value));
            else if (field == "openParticipation") openParticipation = static_cast<uint32_t>(std::stoul(value));
            else if (field == "participationCount") participationCount = static_cast<uint32_t>(std::stoul(value));
            else if (field == "progressScore") progressScore = static_cast<uint32_t>(std::stoul(value));
        }
        freeReplyObject(reply);

        if (sessionBlob.empty()) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        nex::rmc::SessionInfo info;
        auto session = std::make_shared<nex::rmc::MatchmakeSession>(deserializeMatchmakeSession(sessionBlob, minorVersion));
        session->openParticipation = openParticipation != 0;
        session->participationCount = participationCount;
        session->progressScore = static_cast<uint8_t>(progressScore);
        info.session = session;

        reply = static_cast<redisReply*>(redisCommand(ctx, "SMEMBERS %s", playersKey.c_str()));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                info.players.insert(static_cast<uint32_t>(std::stoul(reply->element[i]->str)));
            }
        }
        freeReplyObject(reply);

        {
            std::lock_guard lock(localSplatoonSessionCacheMutex);
            localSplatoonSessionCache[gId] = info;
        }

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonMatchmakeSession completed for GID " + std::to_string(gId));
        taskPtr->complete(std::make_pair(Result::SUCCESS, info));
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<Result> RedisSharedState::deleteSplatoonMatchmakeSession(uint32_t gId) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    {
        std::scoped_lock lock(localSplatoonClientCacheMutex, localSplatoonSessionCacheMutex);
        localSplatoonSessionCache.erase(gId);
        for (auto& [_, clientInfo] : localSplatoonClientCache) {
            auto gatheringIt = std::ranges::find_if(clientInfo.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
                return gathering && gathering->id == gId;
            });
            if (gatheringIt != clientInfo.joinedGatherings.end()) {
                clientInfo.joinedGatherings.erase(gatheringIt);
            }
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, gId](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "deleteSplatoonMatchmakeSession", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "deleteSplatoonMatchmakeSession for GID " + std::to_string(gId));

        const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
        const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);
        const std::string sessionsIndexKey = std::string(REDIS_KEY_PREFIX) + "splatoon:sessions";

        const char* luaScript = R"LUASCRIPT(
local sessionKey = KEYS[1]
local playersKey = KEYS[2]
local sessionsIndex = KEYS[3]
local gid = ARGV[1]
local ttl = tonumber(ARGV[2])
local keyPrefix = ARGV[3]
if redis.call('EXISTS', sessionKey) == 0 then
    return -1
end
redis.call('DEL', sessionKey)
redis.call('DEL', playersKey)
redis.call('SREM', sessionsIndex, gid)
redis.call('EXPIRE', sessionsIndex, ttl)
return count
)LUASCRIPT";

        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx, "EVAL %s 3 %s %s %s %u %u %s",
                luaScript, sessionKey.c_str(), playersKey.c_str(), sessionsIndexKey.c_str(),
                gId, config.clientTTLSeconds, std::string(REDIS_KEY_PREFIX).c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER || reply->integer < 0) {
            freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "deleteSplatoonMatchmakeSession completed for GID " + std::to_string(gId));
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

async::ManualTask<std::pair<Result, std::unordered_map<uint32_t, nex::rmc::SessionInfo>>>
RedisSharedState::getAllSplatoonMatchmakeSessions() {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, std::unordered_map<uint32_t, nex::rmc::SessionInfo>>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getAllSplatoonMatchmakeSessions",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::unordered_map<uint32_t, nex::rmc::SessionInfo>{})); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getAllSplatoonMatchmakeSessions");

        std::unordered_map<uint32_t, nex::rmc::SessionInfo> result;
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "SMEMBERS splatit:splatoon:sessions"));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::unordered_map<uint32_t, nex::rmc::SessionInfo>{}));
            return;
        }

        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                uint32_t gId = static_cast<uint32_t>(std::stoul(reply->element[i]->str));
                const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
                const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);

                auto* sessionReply = static_cast<redisReply*>(redisCommand(ctx, "HGETALL %s", sessionKey.c_str()));
                if (!sessionReply) {
                    continue;
                }
                if (sessionReply->type == REDIS_REPLY_ARRAY && sessionReply->elements == 0) {
                    freeReplyObject(sessionReply);
                    continue;
                }
                if (sessionReply->type != REDIS_REPLY_ARRAY || sessionReply->elements % 2 != 0) {
                    freeReplyObject(sessionReply);
                    continue;
                }

                std::string sessionBlob;
                uint8_t minorVersion = 0;
                uint32_t openParticipation = 0;
                uint32_t participationCount = 0;
                uint32_t progressScore = 0;

                for (size_t j = 0; j < sessionReply->elements; j += 2) {
                    const std::string field(sessionReply->element[j]->str, sessionReply->element[j]->len);
                    const std::string value(sessionReply->element[j + 1]->str, sessionReply->element[j + 1]->len);
                    if (field == "session") sessionBlob = value;
                    else if (field == "minorVersion") minorVersion = static_cast<uint8_t>(std::stoul(value));
                    else if (field == "openParticipation") openParticipation = static_cast<uint32_t>(std::stoul(value));
                    else if (field == "participationCount") participationCount = static_cast<uint32_t>(std::stoul(value));
                    else if (field == "progressScore") progressScore = static_cast<uint32_t>(std::stoul(value));
                }
                freeReplyObject(sessionReply);

                if (sessionBlob.empty()) {
                    continue;
                }

                nex::rmc::SessionInfo info;
                auto session = std::make_shared<nex::rmc::MatchmakeSession>(deserializeMatchmakeSession(sessionBlob, minorVersion));
                session->openParticipation = openParticipation != 0;
                session->participationCount = participationCount;
                session->progressScore = static_cast<uint8_t>(progressScore);
                info.session = session;

                auto* playersReply = static_cast<redisReply*>(redisCommand(ctx, "SMEMBERS %s", playersKey.c_str()));
                if (playersReply) {
                    if (playersReply->type == REDIS_REPLY_ARRAY) {
                        for (size_t j = 0; j < playersReply->elements; ++j) {
                            info.players.insert(static_cast<uint32_t>(std::stoul(playersReply->element[j]->str)));
                        }
                    }
                    freeReplyObject(playersReply);
                }

                result[gId] = info;
            }
        }

        freeReplyObject(reply);
        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getAllSplatoonMatchmakeSessions completed (count=" + std::to_string(result.size()) + ")");
        taskPtr->complete(std::make_pair(Result::SUCCESS, result));
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<Result> RedisSharedState::updateSplatoonMatchmakeSession(const nex::rmc::SessionInfo&& sessionInfo) {
    return setSplatoonMatchmakeSession(std::move(sessionInfo));
}

async::ManualTask<Result> RedisSharedState::addPlayersToSplatoonMatchmakeSession(
    uint32_t gId, const std::vector<uint32_t>& playerPids) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    {
        std::scoped_lock lock(localSplatoonClientCacheMutex, localSplatoonSessionCacheMutex);
        auto sessionIt = localSplatoonSessionCache.find(gId);
        if (sessionIt != localSplatoonSessionCache.end()) {
            for (uint32_t pid : playerPids) {
                sessionIt->second.players.insert(pid);
                if (auto clientIt = localSplatoonClientCache.find(pid); clientIt != localSplatoonClientCache.end()) {
                    auto gatheringIt = std::ranges::find_if(clientIt->second.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
                        return gathering && gathering->id == gId;
                    });
                    if (gatheringIt == clientIt->second.joinedGatherings.end()) {
                        clientIt->second.joinedGatherings.push_back(sessionIt->second.session);
                    } else {
                        *gatheringIt = sessionIt->second.session;
                    }
                }
            }
            if (sessionIt->second.session) {
                sessionIt->second.session->participationCount = static_cast<uint32_t>(sessionIt->second.players.size());
            }
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, gId, playerPids](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "addPlayersToSplatoonMatchmakeSession", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "addPlayersToSplatoonMatchmakeSession for GID " + std::to_string(gId) +
            " (playersToAdd=" + std::to_string(playerPids.size()) + ")");

        const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
        const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);
        const std::string luaScript = R"LUASCRIPT(
local sessionKey=KEYS[1]
local playersKey=KEYS[2]
local gid=ARGV[1]
local ttl=tonumber(ARGV[2])
local keyPrefix=ARGV[3]
if redis.call('EXISTS', sessionKey) == 0 then
    return -1
end
for i=4,#ARGV do
    local pid=ARGV[i]
    redis.call('SADD', playersKey, pid)
    redis.call('SADD', keyPrefix .. 'splatoon:client:gatherings:' .. pid, gid)
    redis.call('EXPIRE', keyPrefix .. 'splatoon:client:gatherings:' .. pid, ttl)
end
local count=redis.call('SCARD', playersKey)
redis.call('HSET', sessionKey, 'participationCount', count)
redis.call('EXPIRE', sessionKey, ttl)
redis.call('EXPIRE', playersKey, ttl)
redis.call('EXPIRE', keyPrefix .. 'splatoon:sessions', ttl)
return count
)LUASCRIPT";

        std::vector<std::string> args;
        args.reserve(10 + playerPids.size());
        args.emplace_back("EVAL");
        args.push_back(luaScript);
        args.emplace_back("2");
        args.push_back(sessionKey);
        args.push_back(playersKey);
        args.push_back(std::to_string(gId));
        args.push_back(std::to_string(config.clientTTLSeconds));
        args.emplace_back(REDIS_KEY_PREFIX);
        for (uint32_t pid : playerPids) {
            args.push_back(std::to_string(pid));
        }

        std::vector<const char*> argv;
        std::vector<size_t> argvlen;
        argv.reserve(args.size());
        argvlen.reserve(args.size());
        for (auto& arg : args) {
            argv.push_back(arg.c_str());
            argvlen.push_back(arg.size());
        }

        auto* reply = static_cast<redisReply*>(redisCommandArgv(ctx, static_cast<int>(argv.size()), argv.data(), argvlen.data()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER || reply->integer < 0) {
            freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        const long long updatedCount = reply->integer;
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "addPlayersToSplatoonMatchmakeSession completed for GID " + std::to_string(gId) +
            " (playerCount=" + std::to_string(updatedCount) + ")");
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<Result> RedisSharedState::removePlayerFromSplatoonMatchmakeSession(uint32_t gId, uint32_t playerPid) {
    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    {
        std::scoped_lock lock(localSplatoonClientCacheMutex, localSplatoonSessionCacheMutex);
        if (auto sessionIt = localSplatoonSessionCache.find(gId); sessionIt != localSplatoonSessionCache.end()) {
            sessionIt->second.players.erase(playerPid);
            if (sessionIt->second.session) {
                sessionIt->second.session->participationCount = static_cast<uint32_t>(sessionIt->second.players.size());
            }
        }
        if (auto clientIt = localSplatoonClientCache.find(playerPid); clientIt != localSplatoonClientCache.end()) {
            auto gatheringIt = std::ranges::find_if(clientIt->second.joinedGatherings, [gId](const std::shared_ptr<nex::rmc::Gathering>& gathering) {
                return gathering && gathering->id == gId;
            });
            if (gatheringIt != clientIt->second.joinedGatherings.end()) {
                clientIt->second.joinedGatherings.erase(gatheringIt);
            }
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, gId, playerPid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "removePlayerFromSplatoonMatchmakeSession", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "removePlayerFromSplatoonMatchmakeSession for GID " + std::to_string(gId) +
            " (playerPid=" + std::to_string(playerPid) + ")");

        const std::string sessionKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:" + std::to_string(gId);
        const std::string playersKey = std::string(REDIS_KEY_PREFIX) + "splatoon:session:players:" + std::to_string(gId);
        const std::string clientGatheringsKey = std::string(REDIS_KEY_PREFIX) + "splatoon:client:gatherings:" + std::to_string(playerPid);

        const char* luaScript = R"LUASCRIPT(
local sessionKey = KEYS[1]
local playersKey = KEYS[2]
local clientGatheringsKey = KEYS[3]
local gid = ARGV[1]
local playerPid = ARGV[2]
local ttl = tonumber(ARGV[3])
local keyPrefix = ARGV[4]
if redis.call('EXISTS', sessionKey) == 0 then
    return -1
end
redis.call('SREM', playersKey, playerPid)
redis.call('SREM', clientGatheringsKey, gid)
local count = redis.call('SCARD', playersKey)
redis.call('HSET', sessionKey, 'participationCount', count)
redis.call('EXPIRE', sessionKey, ttl)
redis.call('EXPIRE', playersKey, ttl)
redis.call('EXPIRE', clientGatheringsKey, ttl)
redis.call('EXPIRE', keyPrefix .. 'splatoon:sessions', ttl)
return count
)LUASCRIPT";

        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx, "EVAL %s 3 %s %s %s %u %u %u %s",
                luaScript, sessionKey.c_str(), playersKey.c_str(), clientGatheringsKey.c_str(),
                gId, playerPid, config.clientTTLSeconds, std::string(REDIS_KEY_PREFIX).c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER || reply->integer < 0) {
            freeReplyObject(reply);
            taskPtr->complete(Result::FAILURE);
            return;
        }
        const long long updatedCount = reply->integer;
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "removePlayerFromSplatoonMatchmakeSession completed for GID " + std::to_string(gId) +
            " (playerCount=" + std::to_string(updatedCount) + ")");
        taskPtr->complete(Result::SUCCESS);
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
    return *task;
}

async::ManualTask<std::pair<Result, uint32_t>> RedisSharedState::getSplatoonMatchmakeSessionCount() {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, uint32_t>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getSplatoonMatchmakeSessionCount",
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL)); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonMatchmakeSessionCount");

        uint32_t count = 0;
        // Use wildcard pattern but exclude keys with additional colons after "session:"
        if (!countKeysByPattern(ctx, std::string(REDIS_KEY_PREFIX) + "splatoon:session:*", count, ":.*:.*:.*:")) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "SCAN count failed for getSplatoonRegisteredClientCount");
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL));
            return;
        }

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonMatchmakeSessionCount completed, count: " + std::to_string(count));
        taskPtr->complete(std::make_pair(Result::SUCCESS, count));
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();

    return *task;
}

} // namespace ss
