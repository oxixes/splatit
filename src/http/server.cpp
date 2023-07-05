#include "server.hpp"

#include <utility>

HTTP_Server::HTTP_Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SocketManager> socketMgr,
                         sock::IPv4Dir listenDir, int keepAliveTimeout, EVP_PKEY* key, X509* cert) {
    this->logger = std::move(logger);
    this->socketMgr = std::move(socketMgr);
    this->keepAliveTimeout = keepAliveTimeout;
    this->mainSocketID = 0;

    auto sslSocket = std::make_shared<sock::SSLSocket>(true, key, cert);
    sslSocket->setBlocking(false);

    int opt = 1;
    sslSocket->setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = listenDir.d << 24 | listenDir.c << 16 | listenDir.b << 8 | listenDir.a;
    address.sin_port = htons(listenDir.port);

    sslSocket->bind((struct sockaddr*)&address, sizeof(address));

    mainSocket = sslSocket;
}

HTTP_Server::~HTTP_Server() {
    stop();
}

void HTTP_Server::listen(int workerCount, const std::function<void()>& closeFunc) {
    shouldStop = false;
    for (int i = 0; i < workerCount; i++) {
        threads.emplace_back(&HTTP_Server::serverThread, this);
    }

    mainSocket->listen();

    std::function<void(unsigned int)> closeCallback = nullptr;
    if (closeFunc != nullptr) {
        closeCallback = [closeFunc] (unsigned int) { closeFunc(); };
    }

    mainSocketID = this->socketMgr->addTCPSocket(mainSocket,
                                                 [this] (unsigned int id, unsigned int newSockId, sock::IPv4Dir dir)
                                                 { onAccept(newSockId, dir); },
                                                 closeCallback,
                                                 [this] (unsigned int id, std::vector<unsigned char> data)
                                                 { onDataReceived(id, std::move(data)); },
                                                 [this] (unsigned int id) { onClose(id); },
                                                 keepAliveTimeout);
}

void HTTP_Server::onAccept(unsigned int newSockId, sock::IPv4Dir dir) {
    buffers[newSockId] = std::vector<unsigned char>();

    std::unique_lock clientsLock(clientsMutex);
    clients[newSockId] = dir;
}

void HTTP_Server::onClose(unsigned int sockId) {
    buffers.erase(sockId);

    std::unique_lock clientsLock(clientsMutex);
    clients.erase(sockId);
}

void HTTP_Server::onDataReceived(unsigned int sockId, std::vector<unsigned char> data) {
    if (data.empty()) return;

    auto& buffer = buffers.find(sockId)->second;
    if (buffer.size() + data.size() > MAX_PAYLOAD_SIZE) {
        sendError(sockId, HTTP_STATUS_PAYLOAD_TOO_LARGE, http::Version::HTTP_1_1);
        return;
    }

    buffer.insert(buffer.end(), data.begin(), data.end());

    try {
        size_t length = 0;
        auto request = http::Request::parse(buffer, length);

        std::unique_lock clientsLock(clientsMutex);
        sock::IPv4Dir dir = clients.find(sockId)->second;
        clientsLock.unlock();

        std::string ipAndPort = std::to_string(dir.a) + "." + std::to_string(dir.b) + "."
                                + std::to_string(dir.c) + "." + std::to_string(dir.d) + ":" + std::to_string(dir.port);

        std::string method;
        switch(request.getMethod()) {
            case http::Method::M_GET:
                method = "GET";
                break;
            case http::Method::M_HEAD:
                method = "HEAD";
                break;
            case http::Method::M_POST:
                method = "POST";
                break;
            case http::Method::M_PUT:
                method = "PUT";
                break;
            case http::Method::M_DELETE:
                method = "DELETE";
                break;
        }

        logger->log(Logger::level::DEBUG, Logger::group::NETWORK, "Received request from " +
                    ipAndPort + " " + method + " " + request.getPath());

        std::unique_lock lock(requestsQueueMutex);
        requestsQueue.emplace(sockId, std::move(request));
        lock.unlock();
        workerCV.notify_one();

        buffer.erase(buffer.begin(), buffer.begin() + (long long) length);
    } catch (http::NotCompleteException& e) {
        // Do nothing, wait for more data
    } catch (http::LengthUnknownException& e) {
        sendError(sockId, HTTP_STATUS_BAD_REQUEST, http::Version::HTTP_1_1);
    } catch (http::VersionNotSupportedException& e) {
        sendError(sockId, HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED, http::Version::HTTP_1_1);
    } catch (http::MethodNotSupportedException& e) {
        sendError(sockId, HTTP_STATUS_METHOD_NOT_ALLOWED, http::Version::HTTP_1_1);
    } catch (http::MalformedException& e) {
        sendError(sockId, HTTP_STATUS_BAD_REQUEST, http::Version::HTTP_1_1);
    }
}

void HTTP_Server::serverThread() {
    while (true) {
        std::unique_lock lock(workerMutex);
        workerCV.wait(lock, [this] { return !requestsQueue.empty() || shouldStop; });

        if (shouldStop) return;

        bool shouldContinue = true;

        while (shouldContinue) {
            std::unique_lock queueLock(requestsQueueMutex);

            if (requestsQueue.empty()) {
                shouldContinue = false;
                continue;
            }

            auto request = std::move(requestsQueue.front());
            requestsQueue.pop();

            shouldContinue = !requestsQueue.empty();
            queueLock.unlock();

            std::unique_lock routesLock(routesMutex);
            auto handlerIt = routes.find(request.second.getPath());
            auto handler = handlerIt == routes.end() ? nullptr : handlerIt->second;
            routesLock.unlock();

            if (handler == nullptr) {
                sendError(request.first, HTTP_STATUS_NOT_FOUND, request.second.getVersion());
            } else {
                std::unique_lock clientsLock(clientsMutex);
                sock::IPv4Dir clientDir = clients[request.first];
                clientsLock.unlock();

                try {
                    bool shouldClose = false;
                    auto response = handler(logger, request.second, clientDir, shouldStop, shouldClose,
                                            [this] (std::function<void()> func) { return registerCloseCall(std::move(func)); },
                                            [this] (unsigned int id) { return unregisterCloseCall(id); });

                    if (!socketMgr->isClosed(request.first)) {
                        socketMgr->send(request.first, std::move(response.serialize()));
                        if (shouldClose) socketMgr->close(request.first);
                    }
                } catch (std::exception& e) {
                    sendError(request.first, HTTP_STATUS_INTERNAL_SERVER_ERROR, request.second.getVersion());
                }
            }
        }
    }
}

http::Response HTTP_Server::getError(http::Version version, int status) {
    http::Response response(version, status);

    response.setHeader("Content-Type", "text/html");
    response.setHeader("Connection", "close");

    std::string statusString = std::to_string(status) + " " + http::STATUS_CODE_MSG.at(status);

    std::string body = "<!DOCTYPE html><html><head><title>" + statusString +
                       "</title></head><body><h1>" + statusString + "</h1></body></html>";
    std::vector<unsigned char> bodyVec(body.begin(), body.end());
    response.setBody(bodyVec);

    return std::move(response);
}

void HTTP_Server::sendError(unsigned int sockId, int status, http::Version version) {
    auto response = getError(version, status);
    socketMgr->send(sockId, std::move(response.serialize()));
    socketMgr->close(sockId);
}

void HTTP_Server::stop() {
    if (shouldStop) return;

    logger->log(Logger::level::INFO, Logger::group::NETWORK, "Stopping HTTP server");

    socketMgr->close(mainSocketID, true);

    shouldStop = true;

    std::unique_lock closeCallsLock(closeCallsMutex);
    for (auto& call : closeCalls) {
        call.second();
    }
    closeCalls.clear();

    workerCV.notify_all();
    for (auto& thread : threads) {
        thread.join();
    }
    threads.clear();

    mainSocket = nullptr;
}

unsigned int HTTP_Server::registerCloseCall(std::function<void()> closeFunc) {
    std::unique_lock lock(closeCallsMutex);
    unsigned int id = closeCallID++;
    closeCalls[id] = std::move(closeFunc);
    return id;
}

void HTTP_Server::unregisterCloseCall(unsigned int id) {
    std::unique_lock lock(closeCallsMutex);
    closeCalls.erase(id);
}

void HTTP_Server::registerRoute(const std::string& path, std::function<http::Response(
        std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Dir, bool&, bool&,
        std::function<unsigned int(std::function<void()>)>, std::function<void(unsigned int)>)> func) {
    std::unique_lock lock(routesMutex);
    routes[path] = std::move(func);
}