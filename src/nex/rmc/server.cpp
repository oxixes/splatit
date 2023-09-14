#include "server.hpp"

#include <utility>

#include "../../exceptions.hpp"

namespace nex::rmc {

Server::Server(std::shared_ptr<Logger::Logger> logger) : logger(std::move(logger)) {
    registerCloseCall = [this](std::function<void()> closeFunc) -> uint32_t {
        if (shouldStop) return 0;
        std::unique_lock lock(closeCallsMutex);
        auto id = closeCallID++;
        closeCalls[id] = std::move(closeFunc);
        return id;
    };

    unregisterCloseCall = [this](uint32_t id) {
        std::unique_lock lock(closeCallsMutex);
        closeCalls.erase(id);
    };
}

void Server::registerPRUDPServer(const std::shared_ptr<prudp::Server>& server, uint8_t listenPort, int workerCount) {
    server->registerRMCServer(listenPort,
                              [this, workerCount]() { start(workerCount); },
                              [this]() { stop(); },
                              [this](prudp::PRUDPAddress addr, uint32_t pid) { onConnect(addr, pid); },
                              [this](prudp::PRUDPAddress addr) { onDisconnect(addr); },
                              [this](prudp::PRUDPAddress addr, uint8_t minor_version, uint8_t substreamId,
                                       std::vector<uint8_t> data) { onData(addr, minor_version, substreamId, std::move(data)); });

    sendData = [server](prudp::PRUDPAddress addr, std::vector<uint8_t> data, uint8_t substreamId) {
        server->sendDataPacket(addr, std::move(data), substreamId);
    };
}

void Server::onConnect(prudp::PRUDPAddress address, uint32_t pid) {
    if (shouldStop) return;

    pidMap[address] = pid;
}

void Server::onDisconnect(prudp::PRUDPAddress address) {
    if (shouldStop) return;

    pidMap.erase(address);
}

void Server::onData(prudp::PRUDPAddress addr, uint8_t minor_version, uint8_t substreamId, std::vector<uint8_t> data) {
    if (shouldStop) return;

    auto request = Request();
    try {
        data = request.decode(data);
    } catch (const NotCompleteException& e) {
        // The request is not complete, we rethrow the exception to notify the underlying PRUDP server
        throw e;
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Received a malformed request from " +
                                                    util::ipv4ToString(addr.address) + ": " +
                                                    std::string(e.what()));
        return;
    } catch (const std::exception& e) {
        logger->log(Logger::level::WARN, logGroup, "An exception occurred while decoding a request from " +
                                                    util::ipv4ToString(addr.address) + ": " +
                                                    std::string(e.what()));
        return;
    }

    logMsg(request, addr, true);

    auto call = calls.find(std::tuple(request.protocolId, request.extendedProtocolId, request.methodId));
    if (call == calls.end()) {
        // The method is not registered, so we send an error response
        logger->log(Logger::level::WARN, logGroup,
                    "Received a request for an unregistered method (protocol id: " +
                    std::to_string(request.protocolId) + ", extended protocol id: " +
                    std::to_string(request.extendedProtocolId) + ", method id: " +
                    std::to_string(request.methodId) + ")");

        sendMsg(ClientInfo{addr, minor_version, substreamId},
                createError(request, Error::CORE__NOT_IMPLEMENTED), {});
        return;
    }

    std::vector<T_ptr> params;
    try {
        params = call->second.parser(minor_version, data);
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Received a malformed parameter data from " +
                                                    util::ipv4ToString(addr.address) + ": " +
                                                    std::string(e.what()));
    } catch (const std::exception& e) {
        logger->log(Logger::level::WARN, logGroup, "An exception occurred while parsing the parameters of a request from " +
                                                    util::ipv4ToString(addr.address) + ": " +
                                                    std::string(e.what()));
    }

    std::unique_lock lock(queueMutex);

    uint32_t pid = 0;
    auto pidIt = pidMap.find(addr);
    if (pidIt != pidMap.end()) {
        pid = pidIt->second;
    }

    requestsQueue.push(RequestInfo{ClientInfo{addr, minor_version, substreamId, pid},
                                   request, std::move(params)});
    lock.unlock();

    workerCV.notify_one();
}

void Server::serverThread() {
    while (true) {
        std::unique_lock lock(workerMutex);
        workerCV.wait(lock, [this] { return !requestsQueue.empty() || shouldStop; });
        lock.unlock();

        if (shouldStop) break;

        bool shouldContinue = true;

        while (shouldContinue && !shouldStop) {
            std::unique_lock queueLock(queueMutex);
            auto reqInfo = requestsQueue.front();
            requestsQueue.pop();

            if (requestsQueue.empty()) shouldContinue = false;
            queueLock.unlock();

            if (shouldStop) continue;

            auto callId = std::tuple(reqInfo.request.protocolId, reqInfo.request.extendedProtocolId,
                                     reqInfo.request.methodId);
            auto call = calls.find(callId);

            // This should never happen, as only existing request are added to the queue, but we check anyway
            if (call == calls.end()) continue;

            try {
                call->second.callback(reqInfo.client, reqInfo.request, reqInfo.params);
            } catch (const std::exception& e) {
                logger->log(Logger::level::FAILURE, logGroup, "An exception occurred while processing a request"
                                                              "(protocol id: " +
                                                              std::to_string(reqInfo.request.protocolId) + ", extended protocol id: " +
                                                              std::to_string(reqInfo.request.extendedProtocolId) + ", method id: " +
                                                              std::to_string(reqInfo.request.methodId) + "): " +
                                                              std::string(e.what()));
            }
        }
    }
}

void Server::start(int workerCount) {
    logger->log(Logger::level::INFO, logGroup,
                "Starting RMC server with " + std::to_string(workerCount) + " workers");

    shouldStop = false;
    for (int i = 0; i < workerCount; i++) {
        threads.emplace_back(&Server::serverThread, this);
    }
}

void Server::stop() {
    if (shouldStop) return;

    logger->log(Logger::level::INFO, logGroup, "Stopping RMC server...");

    shouldStop = true;

    std::unique_lock closeCallsLock(closeCallsMutex);
    for (auto& call : closeCalls) {
        call.second();
    }
    closeCalls.clear();
    closeCallsLock.unlock();

    workerCV.notify_all();
    for (auto& thread : threads) {
        thread.join();
    }
    threads.clear();

    std::unique_lock queueLock(queueMutex);
    requestsQueue = std::queue<RequestInfo>(); // Clear the queue
}

Response Server::createError(const Request& req, Error error) {
    Response res;
    res.protocolId = req.protocolId;
    res.extendedProtocolId = req.extendedProtocolId;
    res.methodId = req.methodId;
    res.callId = req.callId;
    res.success = false;
    res.error = error;

    return res;
}

} // namespace nex::rmc