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
                    if (pidStr.find(':') != std::string::npos ||
                        pidStr.empty() ||
                        pidStr.find_first_not_of("0123456789") != std::string::npos) {
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
                } else if (expiredKey.find("splatit:splatoon:client:") == 0) {
                    // Extract PID from key
                    std::string pidStr = expiredKey.substr(24); // Skip "splatit:splatoon:client:"

                    // Check if this looks like a splatoon subkey (skip those)
                    if (pidStr.find(':') != std::string::npos ||
                        pidStr.empty() ||
                        pidStr.find_first_not_of("0123456789") != std::string::npos) {
                        freeReplyObject(reply);
                        continue;
                    }

                    try {
                        uint32_t pid = std::stoul(pidStr);

                        logger->log(Logger::level::INFO, Logger::group::REDIS,
                            "Detected expiration of Splatoon client key for PID " + std::to_string(pid));

                        // Use Lua script with marker key to ensure only one server decrements
                        Task decrementTask;
                        decrementTask.operation = [this, pid](redisContext* taskCtx) {
                            std::string markerKey = "splatitexpiration:splatoon:client:" + std::to_string(pid);

                            const char* luaScript = R"LUASCRIPT(
local markerKey = KEYS[1]
local wasSet = redis.call('SET', markerKey, '1', 'NX', 'EX', 60)
if wasSet then
    return 1
else
    return 0
end
)LUASCRIPT";

                            auto* decrementReply = static_cast<redisReply*>(
                                redisCommand(taskCtx, "EVAL %s 1 %s", luaScript, markerKey.c_str()));

                            if (decrementReply && decrementReply->type == REDIS_REPLY_INTEGER) {
                                if (decrementReply->integer == 1) {
                                    decrementSplatoonClientCount(taskCtx);
                                    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                                        "Splatoon client counter decremented for expired PID " + std::to_string(pid));
                                } else {
                                    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                                        "Another instance already decremented Splatoon client counter for PID " + std::to_string(pid));
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
                            "Failed to parse Splatoon PID from expired key: " + expiredKey);
                    }
                } else if (expiredKey.find("splatit:splatoon:session:") == 0) {
                    // Extract GID from key
                    std::string gidStr = expiredKey.substr(25); // Skip "splatit:splatoon:session:"

                    // Check if this looks like a splatoon subkey (skip those)
                    if (gidStr.find(':') != std::string::npos ||
                        gidStr.empty() ||
                        gidStr.find_first_not_of("0123456789") != std::string::npos) {
                        freeReplyObject(reply);
                        continue;
                    }

                    try {
                        uint32_t gId = std::stoul(gidStr);

                        logger->log(Logger::level::INFO, Logger::group::REDIS,
                            "Detected expiration of Splatoon session key for GID " + std::to_string(gId));

                        // Use Lua script with marker key to ensure only one server decrements
                        Task decrementTask;
                        decrementTask.operation = [this, gId](redisContext* taskCtx) {
                            std::string markerKey = "splatitexpiration:splatoon:session:" + std::to_string(gId);

                            const char* luaScript = R"LUASCRIPT(
local markerKey = KEYS[1]
local wasSet = redis.call('SET', markerKey, '1', 'NX', 'EX', 60)
if wasSet then
    return 1
else
    return 0
end
)LUASCRIPT";

                            auto* decrementReply = static_cast<redisReply*>(
                                redisCommand(taskCtx, "EVAL %s 1 %s", luaScript, markerKey.c_str()));

                            if (decrementReply && decrementReply->type == REDIS_REPLY_INTEGER) {
                                if (decrementReply->integer == 1) {
                                    decrementSplatoonGatheringCount(taskCtx);
                                    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                                        "Splatoon session counter decremented for expired GID " + std::to_string(gId));
                                } else {
                                    logger->log(Logger::level::DEBUG, Logger::group::REDIS,
                                        "Another instance already decremented Splatoon session counter for GID " + std::to_string(gId));
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
                            "Failed to parse Splatoon GID from expired key: " + expiredKey);
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

        reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:client:count %u", config.clientTTLSeconds));
        if (reply) freeReplyObject(reply);
        reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:session:count %u", config.clientTTLSeconds));
        if (reply) freeReplyObject(reply);
        reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:sessions %u", config.clientTTLSeconds));
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
        std::lock_guard lock(localClientCacheMutex);
        clientsToRecreate = localClientCache;
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

        uint32_t recreatedSplatoonClients = 0;
        uint32_t refreshedSplatoonClients = 0;
        uint32_t recreatedSplatoonSessions = 0;
        uint32_t refreshedSplatoonSessions = 0;

        for (const auto& [gId, sessionInfo] : splatoonSessionsToRecreate) {
            if (!sessionInfo.session) {
                continue;
            }

            const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
            auto* existsReply = static_cast<redisReply*>(redisCommand(ctx, "EXISTS %s", sessionKey.c_str()));
            bool exists = existsReply && existsReply->type == REDIS_REPLY_INTEGER && existsReply->integer == 1;
            if (existsReply) freeReplyObject(existsReply);

            const auto sessionCopy = *sessionInfo.session;
            const std::string sessionBlob = serializeMatchmakeSession(sessionCopy);
            const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);

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
                    ("splatit:splatoon:client:gatherings:" + std::to_string(pid)).c_str(), gId));
                if (reply) freeReplyObject(reply);
            }
            reply = static_cast<redisReply*>(redisCommand(ctx, "SADD splatit:splatoon:sessions %u", gId));
            if (reply) freeReplyObject(reply);
            setSplatoonSessionTTL(ctx, gId);

            if (!exists) {
                incrementSplatoonGatheringCount(ctx);
                recreatedSplatoonSessions++;
            } else {
                refreshedSplatoonSessions++;
            }
        }

        for (const auto& [pid, clientInfo] : splatoonClientsToRecreate) {
            const std::string clientKey = "splatit:splatoon:client:" + std::to_string(pid);
            const std::string urlsKey = "splatit:splatoon:client:urls:" + std::to_string(pid);
            const std::string gatheringsKey = "splatit:splatoon:client:gatherings:" + std::to_string(pid);

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
                incrementSplatoonClientCount(ctx);
                recreatedSplatoonClients++;
            } else {
                refreshedSplatoonClients++;
            }
        }

        expireReply = static_cast<redisReply*>(redisCommand(ctx, "SET splatit:splatoon:client:count %u", static_cast<uint32_t>(splatoonClientsToRecreate.size())));
        if (expireReply) freeReplyObject(expireReply);
        expireReply = static_cast<redisReply*>(redisCommand(ctx, "SET splatit:splatoon:session:count %u", static_cast<uint32_t>(splatoonSessionsToRecreate.size())));
        if (expireReply) freeReplyObject(expireReply);

        expireReply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:sessions %u", config.clientTTLSeconds));
        if (expireReply) freeReplyObject(expireReply);
        expireReply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:client:count %u", config.clientTTLSeconds));
        if (expireReply) freeReplyObject(expireReply);
        expireReply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:session:count %u", config.clientTTLSeconds));
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

void RedisSharedState::setSplatoonClientTTL(redisContext* ctx, uint32_t pid) const {
    const std::string clientKey = "splatit:splatoon:client:" + std::to_string(pid);
    const std::string urlsKey = "splatit:splatoon:client:urls:" + std::to_string(pid);
    const std::string gatheringsKey = "splatit:splatoon:client:gatherings:" + std::to_string(pid);

    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", clientKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", urlsKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", gatheringsKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::setSplatoonSessionTTL(redisContext* ctx, uint32_t gId) const {
    const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
    const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);

    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", sessionKey.c_str(), config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE %s %u", playersKey.c_str(), config.clientTTLSeconds));
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

void RedisSharedState::incrementSplatoonClientCount(redisContext* ctx) const {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "INCR splatit:splatoon:client:count"));
    if (reply) freeReplyObject(reply);
    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:client:count %u", config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::decrementSplatoonClientCount(redisContext* ctx) const {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "DECR splatit:splatoon:client:count"));
    if (reply) {
        if (reply->type == REDIS_REPLY_INTEGER && reply->integer < 0) {
            freeReplyObject(reply);
            reply = static_cast<redisReply*>(redisCommand(ctx, "SET splatit:splatoon:client:count 0"));
            if (reply) freeReplyObject(reply);
            reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:client:count %u", config.clientTTLSeconds));
            if (reply) freeReplyObject(reply);
            return;
        }
        freeReplyObject(reply);
    }
    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:client:count %u", config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::incrementSplatoonGatheringCount(redisContext* ctx) const {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "INCR splatit:splatoon:session:count"));
    if (reply) freeReplyObject(reply);
    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:session:count %u", config.clientTTLSeconds));
    if (reply) freeReplyObject(reply);
}

void RedisSharedState::decrementSplatoonGatheringCount(redisContext* ctx) const {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "DECR splatit:splatoon:session:count"));
    if (reply) {
        if (reply->type == REDIS_REPLY_INTEGER && reply->integer < 0) {
            freeReplyObject(reply);
            reply = static_cast<redisReply*>(redisCommand(ctx, "SET splatit:splatoon:session:count 0"));
            if (reply) freeReplyObject(reply);
            reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:session:count %u", config.clientTTLSeconds));
            if (reply) freeReplyObject(reply);
            return;
        }
        freeReplyObject(reply);
    }
    reply = static_cast<redisReply*>(redisCommand(ctx, "EXPIRE splatit:splatoon:session:count %u", config.clientTTLSeconds));
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
    {
        std::lock_guard lock(localClientCacheMutex);
        if (localClientCache.contains(pid)) {
            logger->log(Logger::level::DEBUG, Logger::group::REDIS, "getFriendsRegisteredClientInfo cache hit for PID " + std::to_string(pid));
            task->complete(std::make_pair(Result::SUCCESS, localClientCache[pid]));
            return *task;
        }
    }

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

async::ManualTask<Result> RedisSharedState::updatePreferenceInRegisteredFriendsClientInfo(
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

async::ManualTask<std::pair<Result, uint64_t>> RedisSharedState::getFriendsRegisteredClientCount() {
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

        const std::string clientKey = "splatit:splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = "splatit:splatoon:client:urls:" + std::to_string(pid);
        const std::string gatheringsKey = "splatit:splatoon:client:gatherings:" + std::to_string(pid);

        const std::string luaScript = R"LUASCRIPT(
local clientKey=KEYS[1]
local urlsKey=KEYS[2]
local gatheringsKey=KEYS[3]
local clientCountKey=KEYS[4]
local exists=redis.call('EXISTS', clientKey)
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
if exists == 0 then
    redis.call('INCR', clientCountKey)
end
redis.call('EXPIRE', clientKey, ttl)
redis.call('EXPIRE', urlsKey, ttl)
redis.call('EXPIRE', gatheringsKey, ttl)
redis.call('EXPIRE', clientCountKey, ttl)
return exists
)LUASCRIPT";

        std::vector<std::string> args;
        args.reserve(14 + encodedUrls.size() + gatheringIds.size());
        args.emplace_back("EVAL");
        args.push_back(luaScript);
        args.emplace_back("4");
        args.push_back(clientKey);
        args.push_back(urlsKey);
        args.push_back(gatheringsKey);
        args.emplace_back("splatit:splatoon:client:count");
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

    Task redisTask;
    redisTask.operation = [this, taskPtr, pid](redisContext* ctx) {
        VALIDATE_REDIS_CONTEXT(ctx, "getSplatoonRegisteredClientInfo",
            taskPtr->complete(std::make_pair(Result::FAILURE, std::nullopt)); return);

        logger->log(Logger::level::DEBUG, Logger::group::REDIS,
            "getSplatoonRegisteredClientInfo for PID " + std::to_string(pid));

        const std::string clientKey = "splatit:splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = "splatit:splatoon:client:urls:" + std::to_string(pid);
        const std::string gatheringsKey = "splatit:splatoon:client:gatherings:" + std::to_string(pid);

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
                const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);

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

        const std::string clientKey = "splatit:splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = "splatit:splatoon:client:urls:" + std::to_string(pid);
        const std::string gatheringsKey = "splatit:splatoon:client:gatherings:" + std::to_string(pid);

        const char* luaScript = R"LUASCRIPT(
local clientKey = KEYS[1]
local urlsKey = KEYS[2]
local gatheringsKey = KEYS[3]
local pid = ARGV[1]
local gatherings = redis.call('SMEMBERS', gatheringsKey)
local existedClient = redis.call('EXISTS', clientKey)
for _,gid in ipairs(gatherings) do
    local playersKey = 'splatit:splatoon:session:players:' .. gid
    local sessionKey = 'splatit:splatoon:session:' .. gid
    redis.call('SREM', playersKey, pid)
    local count = redis.call('SCARD', playersKey)
    redis.call('HSET', sessionKey, 'participationCount', count)
    redis.call('EXPIRE', playersKey, tonumber(ARGV[2]))
    redis.call('EXPIRE', sessionKey, tonumber(ARGV[2]))
end
redis.call('DEL', clientKey)
redis.call('DEL', urlsKey)
redis.call('DEL', gatheringsKey)
if existedClient == 1 then
    redis.call('DECR', 'splatit:splatoon:client:count')
    if tonumber(redis.call('GET', 'splatit:splatoon:client:count') or '0') < 0 then
        redis.call('SET', 'splatit:splatoon:client:count', '0')
    end
end
redis.call('EXPIRE', 'splatit:splatoon:client:count', tonumber(ARGV[2]))
return 1
)LUASCRIPT";

        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx, "EVAL %s 3 %s %s %s %u %u",
                luaScript, clientKey.c_str(), urlsKey.c_str(), gatheringsKey.c_str(), pid, config.clientTTLSeconds));
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

        const std::string clientKey = "splatit:splatoon:client:" + std::to_string(pid);
        const std::string urlsKey = "splatit:splatoon:client:urls:" + std::to_string(pid);

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

        const std::string clientKey = "splatit:splatoon:client:" + std::to_string(pid);
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

        const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
        const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);
        const std::string sessionsIndexKey = "splatit:splatoon:sessions";
        const std::string luaScript = R"LUASCRIPT(
local sessionKey=KEYS[1]
local playersKey=KEYS[2]
local sessionsIndex=KEYS[3]
local gatheringCountKey=KEYS[4]
local gid=ARGV[1]
local ttl=tonumber(ARGV[7])
local existed=redis.call('EXISTS', sessionKey)
local oldPlayers=redis.call('SMEMBERS', playersKey)
for _,pid in ipairs(oldPlayers) do
    redis.call('SREM', 'splatit:splatoon:client:gatherings:' .. pid, gid)
    redis.call('EXPIRE', 'splatit:splatoon:client:gatherings:' .. pid, ttl)
end
redis.call('DEL', playersKey)
for i=8,#ARGV do
    local pid=ARGV[i]
    redis.call('SADD', playersKey, pid)
    redis.call('SADD', 'splatit:splatoon:client:gatherings:' .. pid, gid)
    redis.call('EXPIRE', 'splatit:splatoon:client:gatherings:' .. pid, ttl)
end
redis.call('HSET', sessionKey,
    'session', ARGV[2],
    'minorVersion', ARGV[3],
    'openParticipation', ARGV[4],
    'participationCount', ARGV[5],
    'progressScore', ARGV[6])
redis.call('SADD', sessionsIndex, gid)
if existed == 0 then
    redis.call('INCR', gatheringCountKey)
end
redis.call('EXPIRE', sessionKey, ttl)
redis.call('EXPIRE', playersKey, ttl)
redis.call('EXPIRE', sessionsIndex, ttl)
redis.call('EXPIRE', gatheringCountKey, ttl)
return 1
)LUASCRIPT";

        std::vector<std::string> args;
        args.reserve(14 + players.size());
        args.emplace_back("EVAL");
        args.push_back(luaScript);
        args.emplace_back("4");
        args.push_back(sessionKey);
        args.push_back(playersKey);
        args.push_back(sessionsIndexKey);
        args.emplace_back("splatit:splatoon:session:count");
        args.push_back(std::to_string(gId));
        args.push_back(sessionBlob);
        args.push_back(std::to_string(static_cast<uint32_t>(minorVersion)));
        args.push_back(std::to_string(openParticipation));
        args.push_back(std::to_string(participationCount));
        args.push_back(std::to_string(progressScore));
        args.push_back(std::to_string(config.clientTTLSeconds));
        for (uint32_t pid : players) {
            args.push_back(std::to_string(pid));
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

        const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
        const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);

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

        const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
        const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);
        const std::string sessionsIndexKey = "splatit:splatoon:sessions";

        const char* luaScript = R"LUASCRIPT(
local sessionKey = KEYS[1]
local playersKey = KEYS[2]
local sessionsIndex = KEYS[3]
local gatheringCountKey = KEYS[4]
local gid = ARGV[1]
local ttl = tonumber(ARGV[2])
local existed = redis.call('EXISTS', sessionKey)
local players = redis.call('SMEMBERS', playersKey)
for _,pid in ipairs(players) do
    redis.call('SREM', 'splatit:splatoon:client:gatherings:' .. pid, gid)
    redis.call('EXPIRE', 'splatit:splatoon:client:gatherings:' .. pid, ttl)
end
redis.call('DEL', sessionKey)
redis.call('DEL', playersKey)
redis.call('SREM', sessionsIndex, gid)
if existed == 1 then
    redis.call('DECR', gatheringCountKey)
    if tonumber(redis.call('GET', gatheringCountKey) or '0') < 0 then
        redis.call('SET', gatheringCountKey, '0')
    end
end
redis.call('EXPIRE', gatheringCountKey, ttl)
redis.call('EXPIRE', sessionsIndex, ttl)
return 1
)LUASCRIPT";

        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx, "EVAL %s 4 %s %s %s %s %u %u",
                luaScript, sessionKey.c_str(), playersKey.c_str(), sessionsIndexKey.c_str(),
                "splatit:splatoon:session:count", gId, config.clientTTLSeconds));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
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
                const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
                const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);

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

        const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
        const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);
        const std::string luaScript = R"LUASCRIPT(
local sessionKey=KEYS[1]
local playersKey=KEYS[2]
local gid=ARGV[1]
local ttl=tonumber(ARGV[2])
if redis.call('EXISTS', sessionKey) == 0 then
    return -1
end
for i=3,#ARGV do
    local pid=ARGV[i]
    redis.call('SADD', playersKey, pid)
    redis.call('SADD', 'splatit:splatoon:client:gatherings:' .. pid, gid)
    redis.call('EXPIRE', 'splatit:splatoon:client:gatherings:' .. pid, ttl)
end
local count=redis.call('SCARD', playersKey)
redis.call('HSET', sessionKey, 'participationCount', count)
redis.call('EXPIRE', sessionKey, ttl)
redis.call('EXPIRE', playersKey, ttl)
redis.call('EXPIRE', 'splatit:splatoon:sessions', ttl)
redis.call('EXPIRE', 'splatit:splatoon:session:count', ttl)
return count
)LUASCRIPT";

        std::vector<std::string> args;
        args.reserve(9 + playerPids.size());
        args.emplace_back("EVAL");
        args.push_back(luaScript);
        args.emplace_back("2");
        args.push_back(sessionKey);
        args.push_back(playersKey);
        args.push_back(std::to_string(gId));
        args.push_back(std::to_string(config.clientTTLSeconds));
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

        const std::string sessionKey = "splatit:splatoon:session:" + std::to_string(gId);
        const std::string playersKey = "splatit:splatoon:session:players:" + std::to_string(gId);
        const std::string clientGatheringsKey = "splatit:splatoon:client:gatherings:" + std::to_string(playerPid);

        const char* luaScript = R"LUASCRIPT(
local sessionKey = KEYS[1]
local playersKey = KEYS[2]
local clientGatheringsKey = KEYS[3]
local gid = ARGV[1]
local playerPid = ARGV[2]
local ttl = tonumber(ARGV[3])
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
redis.call('EXPIRE', 'splatit:splatoon:sessions', ttl)
redis.call('EXPIRE', 'splatit:splatoon:session:count', ttl)
return count
)LUASCRIPT";

        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx, "EVAL %s 3 %s %s %s %u %u %u",
                luaScript, sessionKey.c_str(), playersKey.c_str(), clientGatheringsKey.c_str(),
                gId, playerPid, config.clientTTLSeconds));
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

} // namespace ss
