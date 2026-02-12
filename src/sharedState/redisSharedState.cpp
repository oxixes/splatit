#include "redisSharedState.hpp"
#include "../socket/socket.hpp"
#include "../nex/prudp/server.hpp"
#include "../nex/friends/friendsSecure.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cerrno>

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

    // Enable keyspace notifications for expired events
    // K = keyspace events (published with __keyspace@<db>__ prefix)
    // E = keyevent events (published with __keyevent@<db>__ prefix)
    // x = expired events
    // We use Kx to get keyspace notifications which include the key name in the channel
    auto* reply = static_cast<redisReply*>(redisCommand(testCtx, "CONFIG SET notify-keyspace-events KEx"));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        logger->log(Logger::level::WARN, Logger::group::REDIS,
            "Failed to enable keyspace notifications (may require admin privileges)");
        if (reply) freeReplyObject(reply);
        // Continue anyway - the counter will still work, just won't auto-decrement on expiration
    } else {
        logger->log(Logger::level::INFO, Logger::group::REDIS, "Keyspace notifications enabled");
        freeReplyObject(reply);
    }

    closeConnection(testCtx);

    logger->log(Logger::level::INFO, Logger::group::REDIS,
        "Starting " + std::to_string(config.workerThreads) + " worker threads");

    // Start worker threads
    running = true;
    for (uint32_t i = 0; i < config.workerThreads; ++i) {
        workers.emplace_back(&RedisSharedState::workerThread, this, i);
    }

    // Start expiration listener thread
    expirationListenerThread = std::thread(&RedisSharedState::expirationListener, this);

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

    if (expirationListenerThread.joinable()) {
        expirationListenerThread.join();
    }

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

void RedisSharedState::expirationListener() {
    logger->log(Logger::level::INFO, Logger::group::REDIS, "Expiration listener thread started");

    redisContext* ctx = createConnection();
    if (!ctx) {
        logger->log(Logger::level::FAILURE, Logger::group::REDIS,
            "Expiration listener failed to create connection");
        return;
    }

    // Subscribe to keyspace notifications for expired events of splatit:* keys only
    // Using keyspace (not keyevent) allows us to filter by key pattern
    std::string pattern = "__keyspace@" + std::to_string(config.database) + "__:splatit:*";
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "PSUBSCRIBE %s", pattern.c_str()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        logger->log(Logger::level::FAILURE, Logger::group::REDIS,
            "Failed to subscribe to expiration events");
        if (reply) freeReplyObject(reply);
        closeConnection(ctx);
        return;
    }
    freeReplyObject(reply);

    logger->log(Logger::level::INFO, Logger::group::REDIS,
        "Subscribed to expiration events on pattern: " + pattern);

    // Listen for expiration events
    while (running) {
        reply = nullptr;

        // Use a timeout to allow periodic checking of running flag
        timeval timeout = {};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        // Set receive timeout
        if (redisSetTimeout(ctx, timeout) != REDIS_OK) {
            logger->log(Logger::level::WARN, Logger::group::REDIS,
                "Failed to set timeout on expiration listener");
        }

        int getReplyResult = redisGetReply(ctx, reinterpret_cast<void**>(&reply));

        if (getReplyResult != REDIS_OK) {
            if (!running) {
                break; // Normal shutdown
            }

            // Check if it's a timeout (which is normal for pub/sub with no messages)
            // When redisGetReply times out, it may set REDIS_ERR_IO with errno=EAGAIN or EWOULDBLOCK
            if (ctx->err == REDIS_ERR_IO && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Normal timeout, no messages available
                continue;
            }

            // ctx->err == 0 also means no real error
            if (ctx->err == 0) {
                continue;
            }

            // Real connection error (EOF or actual I/O error that's not a timeout)
            if (ctx->err == REDIS_ERR_IO || ctx->err == REDIS_ERR_EOF) {
                // Connection lost, try to reconnect
                logger->log(Logger::level::WARN, Logger::group::REDIS,
                    "Expiration listener lost connection (err=" + std::to_string(ctx->err) +
                    ", errno=" + std::to_string(errno) + "), reconnecting...");
                closeConnection(ctx);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                ctx = createConnection();
                if (!ctx) {
                    logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                        "Expiration listener failed to reconnect");
                    return;
                }

                // Re-subscribe
                std::string resubPattern = "__keyspace@" + std::to_string(config.database) + "__:splatit:*";
                reply = static_cast<redisReply*>(redisCommand(ctx, "PSUBSCRIBE %s", resubPattern.c_str()));
                if (reply) freeReplyObject(reply);
            }

            // Other error, just continue
            continue;
        }

        if (!reply) {
            continue; // Timeout or no data
        }

        // Process the message
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 4) {
            std::string messageType(reply->element[0]->str, reply->element[0]->len);

            if (messageType == "pmessage") {
                // Keyspace notification format:
                // element[0] = "pmessage"
                // element[1] = pattern matched
                // element[2] = channel = "__keyspace@{db}__:{key}"
                // element[3] = event type = "expired"

                std::string channel(reply->element[2]->str, reply->element[2]->len);
                std::string event(reply->element[3]->str, reply->element[3]->len);

                // Only process "expired" events
                if (event != "expired") {
                    freeReplyObject(reply);
                    continue;
                }

                // Extract key from channel: "__keyspace@0__:splatit:friends:client:123" -> "splatit:friends:client:123"
                size_t colonPos = channel.find("__:");
                if (colonPos == std::string::npos) {
                    freeReplyObject(reply);
                    continue;
                }
                std::string expiredKey = channel.substr(colonPos + 3); // Skip "__:"

                // Verify it's a splatit friends client key
                if (expiredKey.find("splatit:friends:client:") == 0) {
                    // Extract PID from key
                    std::string pidStr = expiredKey.substr(23); // Skip "splatit:friends:client:"

                    // Check if this looks like a friends list key (skip those)
                    if (pidStr.find(':') != std::string::npos) {
                        freeReplyObject(reply);
                        continue;
                    }

                    try {
                        uint32_t pid = std::stoul(pidStr);

                        logger->log(Logger::level::INFO, Logger::group::REDIS,
                            "Detected expiration of client key for PID " + std::to_string(pid));

                        // Remove from tracked PIDs
                        {
                            std::lock_guard lock(registeredPIDsMutex);
                            registeredPIDs.erase(pid);
                        }

                        // Use Lua script with marker key to ensure only one server decrements
                        Task decrementTask;
                        decrementTask.operation = [this, pid](redisContext* taskCtx) {
                            // Use a marker key to ensure only one server decrements
                            std::string markerKey = "splatitexpiration:friends:client:" + std::to_string(pid);

                            // Lua script: SET NX (only if not exists) + DECR
                            const char* luaScript = R"LUASCRIPT(
local markerKey = KEYS[1]
local wasSet = redis.call('SET', markerKey, '1', 'NX', 'EX', 60)
if wasSet then
    redis.call('DECR', 'splatit:friends:client:count')
    return 1
else
    return 0
end
)LUASCRIPT";

                            auto* decrementReply = static_cast<redisReply*>(
                                redisCommand(taskCtx, "EVAL %s 1 %s", luaScript, markerKey.c_str()));

                            if (decrementReply && decrementReply->type == REDIS_REPLY_INTEGER) {
                                if (decrementReply->integer == 1) {
                                    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                                        "Counter decremented for expired PID " + std::to_string(pid));
                                } else {
                                    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                                        "Another instance already decremented for PID " + std::to_string(pid));
                                }
                            }

                            if (decrementReply) freeReplyObject(decrementReply);
                        };

                        {
                            std::lock_guard lock(queueMutex);
                            taskQueue.push(std::move(decrementTask));
                        }
                        queueCV.notify_one();

                    } catch (const std::exception&) {
                        logger->log(Logger::level::WARN, Logger::group::REDIS,
                            "Failed to parse PID from expired key: " + expiredKey);
                    }
                }
            }
        }

        freeReplyObject(reply);
    }

    closeConnection(ctx);
    logger->log(Logger::level::INFO, Logger::group::REDIS, "Expiration listener thread stopped");
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
        std::string serviceKey = "splatit:server:" + std::to_string(serverId);
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
    {
        std::lock_guard lock(registeredPIDsMutex);
        pidsToRefresh = registeredPIDs;
    }

    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
        "Refreshing TTL for " + std::to_string(pidsToRefresh.size()) + " registered clients, counters and server");

    // Create a task to refresh TTLs
    auto task = std::make_shared<async::ManualTask<void>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr, pidsToRefresh](redisContext* ctx) {
        // Refresh client TTLs
        for (uint32_t pid : pidsToRefresh) {
            setTTL(ctx, pid);
        }

        // Refresh server service key TTL
        std::string serviceKey = "splatit:server:" + std::to_string(serverId);
        uint32_t serviceKeyTTL = config.clientTTLSeconds * 2;
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", serviceKey.c_str(), serviceKeyTTL));
        if (reply) {
            if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1) {
                logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                    "Service key TTL refreshed to " + std::to_string(serviceKeyTTL) + " seconds");
            }
            freeReplyObject(reply);
        }

        // Refresh counter TTL (set to 2x client TTL to ensure it persists longer than any client)
        uint32_t counterTTL = config.clientTTLSeconds * 2;
        reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:friends:client:count %u", counterTTL));
        if (reply) {
            if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1) {
                logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                    "Counter TTL refreshed to " + std::to_string(counterTTL) + " seconds");
            }
            freeReplyObject(reply);
        }

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
    {
        std::lock_guard lock(localClientCacheMutex);
        clientsToRecreate = localClientCache;
    }

    if (clientsToRecreate.empty()) {
        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "No clients to recreate after reconnection");
        return;
    }

    logger->log(Logger::level::INFO, Logger::group::REDIS,
        "Recreating " + std::to_string(clientsToRecreate.size()) + " clients after reconnection");

    // Create a task to recreate all clients
    Task redisTask;
    redisTask.operation = [this, clientsToRecreate](redisContext* ctx) {
        uint32_t recreatedCount = 0;
        uint32_t refreshedCount = 0;

        for (const auto& [pid, clientInfo] : clientsToRecreate) {
            std::string clientKey = "splatit:friends:client:" + std::to_string(pid);
            std::string friendsKey = "splatit:friends:list:" + std::to_string(pid);

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

                // Use Lua script to recreate and increment counter
                const char* luaScript = R"LUASCRIPT(
local key = KEYS[1]
local clientData = ARGV[1]
local preferenceData = ARGV[2]
local counterTTL = tonumber(ARGV[3])
local exists = redis.call('EXISTS', key)
redis.call('HSET', key, 'client', clientData)
redis.call('HSET', key, 'preference', preferenceData)
if exists == 0 then
    redis.call('INCR', 'splatit:friends:client:count')
end
redis.call('EXPIRE', 'splatit:friends:client:count', counterTTL)
return exists
)LUASCRIPT";

                uint32_t counterTTL = config.clientTTLSeconds * 2;
                auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EVAL %s 1 %s %b %b %u",
                    luaScript, clientKey.c_str(),
                    clientInfoStr.c_str(), clientInfoStr.size(),
                    preferenceStr.c_str(), preferenceStr.size(),
                    counterTTL));
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

        // Refresh counter TTL (already done in the Lua script, but ensure it's set)
        uint32_t counterTTL = config.clientTTLSeconds * 2;
        auto* expireReply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:friends:client:count %u", counterTTL));
        if (expireReply) freeReplyObject(expireReply);

        logger->log(Logger::level::INFO, Logger::group::REDIS,
            "Recreation complete: " + std::to_string(recreatedCount) + " recreated, " +
            std::to_string(refreshedCount) + " refreshed");
    };

    {
        std::lock_guard lock(queueMutex);
        taskQueue.push(std::move(redisTask));
    }
    queueCV.notify_one();
}

void RedisSharedState::setTTL(redisContext* ctx, uint32_t pid) {
    std::string clientKey = "splatit:friends:client:" + std::to_string(pid);
    std::string friendsKey = "splatit:friends:list:" + std::to_string(pid);

    // Set TTL for both keys
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u",
        clientKey.c_str(), config.clientTTLSeconds));
    if (reply) {
        if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0) {
            // Key doesn't exist, remove from tracked PIDs and decrement counter
            std::lock_guard lock(registeredPIDsMutex);
            registeredPIDs.erase(pid);
            logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                "PID " + std::to_string(pid) + " key expired, removed from tracking");
            // Note: Counter was already decremented when key expired naturally
        }
        freeReplyObject(reply);
    }

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u",
        friendsKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::incrementClientCount(redisContext* ctx) const {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "INCR splatit:friends:client:count"));
    if (reply) {
        if (reply->type == REDIS_REPLY_INTEGER) {
            logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                "Client count incremented to " + std::to_string(reply->integer));
        }
        freeReplyObject(reply);
    }

    // Set/refresh TTL on counter (2x client TTL to ensure it persists longer than any client)
    uint32_t counterTTL = config.clientTTLSeconds * 2;
    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:friends:client:count %u", counterTTL));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::decrementClientCount(redisContext* ctx) const {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "DECR splatit:friends:client:count"));
    if (reply) {
        if (reply->type == REDIS_REPLY_INTEGER) {
            logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                "Client count decremented to " + std::to_string(reply->integer));

            // Ensure count doesn't go negative
            if (reply->integer < 0) {
                freeReplyObject(reply);
                reply = static_cast<redisReply*>(redisCommand(ctx, "SET splatit:friends:client:count 0"));
                if (reply) freeReplyObject(reply);
                logger->log(Logger::level::WARN, Logger::group::REDIS,
                    "Client count was negative, reset to 0");
            }
        }
        freeReplyObject(reply);
    }
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

// Implementation of SharedState interface methods

async::ManualTask<std::pair<Result, std::optional<std::string>>> RedisSharedState::getPublicFacingRPCAddress(uint32_t serverId) {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, std::optional<std::string>>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr, serverId](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getPublicFacingRPCAddress",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt)); return);

        std::string serviceKey = "splatit:server:" + std::to_string(serverId);
        const auto* reply = static_cast<redisReply*>(redisCommand(ctx, "GET %s", serviceKey.c_str()));
        if (!reply) {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
            return;
        }

        if (reply->type == REDIS_REPLY_STRING) {
            std::string address(reply->str, reply->len);
            taskPtr->complete(std::make_pair(Result::SUCCESS, address));
        } else {
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt));
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
        std::lock_guard lock(localClientCacheMutex);
        localClientCache[pid] = info;
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, clientInfoStr, preferenceStr, friends](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "setFriendsRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        std::string key = "splatit:friends:client:" + std::to_string(pid);
        std::string friendsKey = "splatit:friends:list:" + std::to_string(pid);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "setFriendsRegisteredClientInfo for PID " + std::to_string(pid));

        // Use Lua script for atomic EXISTS + SET + conditional INCR
        // This prevents race conditions in multi-instance environments
        // Always refresh counter TTL to ensure it persists even if all servers disconnect
        const char* luaScript = R"LUASCRIPT(
local key = KEYS[1]
local clientData = ARGV[1]
local preferenceData = ARGV[2]
local counterTTL = tonumber(ARGV[3])
local exists = redis.call('EXISTS', key)
redis.call('HSET', key, 'client', clientData)
redis.call('HSET', key, 'preference', preferenceData)
if exists == 0 then
    redis.call('INCR', 'splatit:friends:client:count')
end
redis.call('EXPIRE', 'splatit:friends:client:count', counterTTL)
return exists
)LUASCRIPT";

        uint32_t counterTTL = config.clientTTLSeconds * 2;
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EVAL %s 1 %s %b %b %u",
            luaScript, key.c_str(),
            clientInfoStr.c_str(), clientInfoStr.size(),
            preferenceStr.c_str(), preferenceStr.size(),
            counterTTL));

        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            if (reply->integer == 0) {
                logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                    "Client " + std::to_string(pid) + " is new, counter incremented and TTL refreshed");
            } else {
                logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                    "Client " + std::to_string(pid) + " already exists, updated and counter TTL refreshed");
            }
        }
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
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getFriendsRegisteredClientInfo",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt)); return);

        std::string key = "splatit:friends:client:" + std::to_string(pid);

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
            ("splatit:friends:list:" + std::to_string(pid)).c_str()));
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
        std::lock_guard lock(localClientCacheMutex);
        localClientCache.erase(pid);
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "deleteFriendsRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "deleteFriendsRegisteredClientInfo for PID " + std::to_string(pid));

        std::string key = "splatit:friends:client:" + std::to_string(pid);
        std::string friendsKey = "splatit:friends:list:" + std::to_string(pid);

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

        // Decrement client count
        decrementClientCount(ctx);

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
        std::lock_guard lock(localClientCacheMutex);
        auto it = localClientCache.find(pid);
        if (it != localClientCache.end()) {
            it->second.friends.insert(friendPid);
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, friendPid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "addFriendToRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "addFriendToRegisteredClientInfo: PID " + std::to_string(pid) + " adding friend " + std::to_string(friendPid));

        std::string friendsKey = "splatit:friends:list:" + std::to_string(pid);

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
        std::lock_guard lock(localClientCacheMutex);
        auto it = localClientCache.find(pid);
        if (it != localClientCache.end()) {
            it->second.friends.erase(friendPid);
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, friendPid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "removeFriendFromRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "removeFriendFromRegisteredClientInfo: PID " + std::to_string(pid) + " removing friend " + std::to_string(friendPid));

        std::string friendsKey = "splatit:friends:list:" + std::to_string(pid);

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

async::ManualTask<Result> RedisSharedState::updatePreferenceInRegisteredClientInfo(
    uint32_t pid, const nex::rmc::UserPreference& preference) {

    auto task = std::make_shared<async::ManualTask<Result>>();
    const auto& taskPtr = task;

    std::string preferenceStr = serializeUserPreference(preference);

    // Update local cache
    {
        std::lock_guard lock(localClientCacheMutex);
        auto it = localClientCache.find(pid);
        if (it != localClientCache.end()) {
            it->second.userData.preference = preference;
        }
    }

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid, preferenceStr](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "updatePreferenceInRegisteredClientInfo", taskPtr->complete(Result::FAILURE); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "updatePreferenceInRegisteredClientInfo for PID " + std::to_string(pid));

        std::string key = "splatit:friends:client:" + std::to_string(pid);

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

async::ManualTask<std::pair<Result, uint64_t>> RedisSharedState::getRegisteredClientCount() {
    auto task = std::make_shared<async::ManualTask<std::pair<Result, uint64_t>>>();
    const auto& taskPtr = task;

    Task redisTask;
    redisTask.operation = [this, taskPtr](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getRegisteredClientCount",
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL)); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getRegisteredClientCount");

        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "GET splatit:friends:client:count"));

        if (!reply) {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "GET failed for getRegisteredClientCount");
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL));
            return;
        }

        uint64_t count = 0;
        if (reply->type == REDIS_REPLY_STRING) {
            try {
                count = std::stoull(reply->str);
            } catch (const std::exception& e) {
                logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                    "Failed to parse client count: " + std::string(e.what()));
                freeReplyObject(reply);
                taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL));
                return;
            }
        } else if (reply->type == REDIS_REPLY_INTEGER) {
            count = static_cast<uint64_t>(reply->integer);
        } else if (reply->type == REDIS_REPLY_NIL) {
            // Key doesn't exist, count is 0
            count = 0;
        } else {
            logger->log(Logger::level::FAILURE, Logger::group::REDIS,
                "Unexpected reply type for getRegisteredClientCount");
            freeReplyObject(reply);
            taskPtr->complete(std::make_pair(Result::FAILURE, 0ULL));
            return;
        }
        freeReplyObject(reply);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getRegisteredClientCount completed, count: " + std::to_string(count));
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