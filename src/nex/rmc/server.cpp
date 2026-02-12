#include "server.hpp"

#include <grpcpp/support/server_callback.h>

#include "../../exceptions.hpp"

namespace nex::rmc {

Server::Server(std::shared_ptr<Logger::Logger> logger, uint32_t serverId) : logger(std::move(logger)), serverId(serverId) {
    this->scheduler = std::make_shared<async::Scheduler>(queueCV);
}

void Server::registerPRUDPServer(const std::shared_ptr<prudp::Server>& server, uint8_t listenPort, int workerCount) {
    server->registerRMCServer(listenPort,
                              [this, workerCount] { start(workerCount); },
                              [this] { stop(); },
                              [this](prudp::PRUDPAddress addr, uint32_t pid) { scheduleOnConnect(addr, pid); },
                              [this](prudp::PRUDPAddress addr) { scheduleOnDisconnect(addr); },
                              [this](prudp::PRUDPAddress addr, uint8_t minor_version, uint8_t substreamId,
                                       std::vector<uint8_t> data) { onData(addr, minor_version, substreamId, std::move(data)); });

    sendData = [server](prudp::PRUDPAddress addr, std::vector<uint8_t> data, uint8_t substreamId) {
        server->sendDataPacket(addr, std::move(data), substreamId);
    };
}

async::Task<void> Server::onConnect(prudp::PRUDPAddress address, uint32_t pid) {
    if (shouldStop) co_return;

    std::unique_lock lock(pidMapMutex);
    pidMap[address] = pid;
}

async::Task<void> Server::onDisconnect(prudp::PRUDPAddress address) {
    if (shouldStop) co_return;

    std::unique_lock lock(pidMapMutex);
    pidMap.erase(address);
}

void Server::onData(prudp::PRUDPAddress addr, uint8_t minor_version, uint8_t substreamId, std::vector<uint8_t> data) {
    if (shouldStop) return;

    auto request = Request();
    try {
        if (data.size() >= 5) {
            uint8_t protocolId = data[4];
            if (!(protocolId & 0x80)) {
                // This is a response, so we ignore it
                try {
                    auto res = Response();
                    res.decode(data);

                    logMsg(res, addr, true);
                } catch (const std::exception& e) {
                    // Ignore, it is a response, so we don't care
                }

                return;
            }
        }

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

        // Log the data that was received
        std::stringstream ss;
        ss << "Data: ";
        for (auto byte : data) {
            ss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte);
        }

        logger->log(Logger::level::DEBUG, logGroup, ss.str());

        sendMsg(ClientInfo{addr, minor_version, substreamId, serverId},
                createError(request, Error::CORE__NOT_IMPLEMENTED), {});
        return;
    }

    std::vector<T_ptr> params;
    try {
        params = call->second.parser(minor_version, data);

        std::unique_lock lock(*queueMutex);
        std::unique_lock pidMapLock(pidMapMutex);
        uint32_t pid = 0;
        auto pidIt = pidMap.find(addr);
        if (pidIt != pidMap.end()) {
            pid = pidIt->second;
        }

        requestsQueue.push(RequestInfo{ClientInfo{addr, minor_version, substreamId, serverId, pid},
                                       request, std::move(params)});
        lock.unlock();

        queueCV->notify_one();
    } catch (const MalformedException& e) {
        logger->log(Logger::level::WARN, logGroup, "Received a malformed parameter data from " +
                                                    util::ipv4ToString(addr.address) + ": " +
                                                    std::string(e.what()));

        sendMsg(ClientInfo{addr, minor_version, substreamId, serverId, 0},
                createError(request, Error::CORE__INVALID_ARGUMENT), {});
    } catch (const std::exception& e) {
        logger->log(Logger::level::WARN, logGroup, "An exception occurred while parsing the parameters of a request from " +
                                                    util::ipv4ToString(addr.address) + ": " +
                                                    std::string(e.what()));

        sendMsg(ClientInfo{addr, minor_version, substreamId, serverId, 0},
                createError(request, Error::CORE__EXCEPTION), {});
    }
}

void Server::scheduleOnConnect(prudp::PRUDPAddress address, uint32_t pid) {
    auto task = std::move(onConnect(address, pid));
    task.setScheduler(scheduler);
    scheduler->schedule(std::move(task));
}

void Server::scheduleOnDisconnect(prudp::PRUDPAddress address) {
    auto task = std::move(onDisconnect(address));
    task.setScheduler(scheduler);
    scheduler->schedule(std::move(task));
}

void Server::serverThread() {
    while (true) {
        std::unique_lock lock(*queueMutex);
        queueCV->wait(lock, [this] { return !requestsQueue.empty() || scheduler->hasTasks() || shouldStop; });

        if (shouldStop) break;

        lock.unlock();
        while (scheduler->hasTasks() && !shouldStop) {
            auto task = scheduler->getTask();
            if (!task) break;

            try {
                async::Scheduler::run(task);
            } catch (const std::exception& e) {
                if (task->getContext().has_value()) {
                    if (task->getContext().type() != typeid(std::pair<ClientInfo, Request>)) {
                        logger->log(Logger::level::FAILURE, logGroup, "An exception occurred while processing a gRPC request: " +
                                                                      std::string(e.what()));
                        const auto reactor = std::any_cast<grpc::ServerUnaryReactor*>(task->getContext());
                        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Internal server error"));
                    } else {
                        auto context = std::any_cast<std::pair<ClientInfo, Request>>(task->getContext());
                        logger->log(Logger::level::FAILURE, logGroup, "An exception occurred while processing a request "
                                                                      "(protocol id: " +
                                                                      std::to_string(context.second.protocolId) + ", extended protocol id: " +
                                                                      std::to_string(context.second.extendedProtocolId) + ", method id: " +
                                                                      std::to_string(context.second.methodId) + "): " +
                                                                      std::string(e.what()));
                        sendMsg(context.first, createError(context.second, Error::CORE__EXCEPTION), {});
                    }
                } else {
                    logger->log(Logger::level::FAILURE, logGroup, "An exception occurred while running a task: " +
                                                                  std::string(e.what()));
                }
            }
        }
        lock.lock();

        if (shouldStop) break;

        while (!requestsQueue.empty() && !shouldStop) {
            auto reqInfo = std::move(requestsQueue.front());
            requestsQueue.pop();

            auto callId = std::tuple(reqInfo.request.protocolId, reqInfo.request.extendedProtocolId,
                                     reqInfo.request.methodId);
            auto call = calls.find(callId);

            // This should never happen, as only existing request are added to the queue, but we check anyway
            if (call == calls.end()) {
                continue;
            }

            auto task = std::move(call->second.callback(reqInfo.client, reqInfo.request, std::move(reqInfo.params)));
            task.setContext(std::make_pair(reqInfo.client, reqInfo.request));
            task.setScheduler(scheduler);
            scheduler->schedule(std::move(task));
        }

        if (shouldStop) break;
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

    queueCV->notify_all();
    for (auto& thread : threads) {
        thread.join();
    }
    threads.clear();

    std::unique_lock queueLock(*queueMutex);
    requestsQueue = std::queue<RequestInfo>(); // Clear the queue
    scheduler->clear();
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

void Server::scheduleArbitraryFunction(async::Task<void>&& task) const {
    task.setScheduler(scheduler);
    scheduler->schedule(std::move(task));
}


} // namespace nex::rmc