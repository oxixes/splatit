#include "server.hpp"

#include <utility>

#include "../exceptions.hpp"

namespace http {

Server::Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SocketManager> socketMgr,
               sock::IPv4Addr listenDir, int keepAliveTimeout, bool ssl, EVP_PKEY* key, X509* cert) {
    this->logger = std::move(logger);
    this->socketMgr = std::move(socketMgr);
    this->keepAliveTimeout = keepAliveTimeout;
    this->mainSocketID = 0;

    std::shared_ptr<sock::TCPSocket> socket;
    if (ssl) {
        socket = std::make_shared<sock::SSLSocket>(true, key, cert);
    } else {
        socket = std::make_shared<sock::TCPSocket>();
    }
    socket->setBlocking(false);

    int opt = 1;
    socket->setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address = util::ipv4ToSockAddr(listenDir);
    socket->bind((struct sockaddr*)&address, sizeof(address));

    mainSocket = socket;
}

Server::~Server() {
    stop();
}

void Server::listen(int workerCount, const std::function<void()>& closeFunc) {
    logger->log(Logger::level::INFO, Logger::group::NETWORK,
                "Starting HTTP server with " + std::to_string(workerCount) + " workers");

    shouldStop = false;
    for (int i = 0; i < workerCount; i++) {
        threads.emplace_back(&Server::serverThread, this);
    }

    mainSocket->listen();

    std::function<void(uint32_t)> closeCallback = nullptr;
    if (closeFunc != nullptr) {
        closeCallback = [closeFunc] (uint32_t) { closeFunc(); };
    }

    mainSocketID = this->socketMgr->addTCPSocket(mainSocket,
                                                 [this] (uint32_t id, uint32_t newSockId, sock::IPv4Addr dir)
                                                 { onAccept(newSockId, dir); },
                                                 closeCallback,
                                                 [this] (uint32_t id, std::vector<uint8_t> data)
                                                 { onDataReceived(id, std::move(data)); },
                                                 [this] (uint32_t id) { onClose(id); },
                                                 keepAliveTimeout);
}

void Server::onAccept(uint32_t newSockId, sock::IPv4Addr dir) {
    buffers[newSockId] = std::vector<uint8_t>();

    std::unique_lock clientsLock(clientsMutex);
    clients[newSockId] = dir;
}

void Server::onClose(uint32_t sockId) {
    buffers.erase(sockId);

    std::unique_lock clientsLock(clientsMutex);
    clients.erase(sockId);
}

void Server::onDataReceived(uint32_t sockId, std::vector<uint8_t> data) {
    if (data.empty()) return;

    auto& buffer = buffers.find(sockId)->second;
    if (buffer.size() + data.size() > MAX_PAYLOAD_SIZE) {
        logger->log(Logger::level::WARN, Logger::group::NETWORK,
                    "Received too much data from " + std::to_string(sockId) + ", closing connection");
        sendError(sockId, HTTP_STATUS_PAYLOAD_TOO_LARGE);
        return;
    }

    buffer.insert(buffer.end(), data.begin(), data.end());

    bool finish = false;
    while (!buffer.empty() && !finish) {
        finish = true;
        try {
            size_t length = 0;
            auto request = Request::parse(buffer, length);

            std::unique_lock clientsLock(clientsMutex);
            sock::IPv4Addr dir = clients.find(sockId)->second;
            clientsLock.unlock();

            std::string ipAndPort = std::to_string(dir.a) + "." + std::to_string(dir.b) + "."
                                    + std::to_string(dir.c) + "." + std::to_string(dir.d) + ":" + std::to_string(dir.port);

            std::string method;
            switch(request.getMethod()) {
                case Method::M_GET:
                    method = "GET";
                    break;
                case Method::M_HEAD:
                    method = "HEAD";
                    break;
                case Method::M_POST:
                    method = "POST";
                    break;
                case Method::M_PUT:
                    method = "PUT";
                    break;
                case Method::M_DELETE:
                    method = "DELETE";
                    break;
            }

            logger->log(Logger::level::DEBUG, Logger::group::NETWORK, "Received request from " +
                                                                      ipAndPort + " " + method + " " + request.getPath());

            std::unique_lock lock(requestsQueueMutex);
            requestsQueue.emplace(sockId, std::move(request));
            lock.unlock();
            workerCV.notify_one();

            buffer.erase(buffer.begin(), buffer.begin() + (ssize_t) length);
            finish = false;
        } catch (NotCompleteException& e) {
            // Do nothing, wait for more data
        } catch (LengthUnknownException& e) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received request with unknown length from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_BAD_REQUEST);
        } catch (VersionNotSupportedException& e) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received request with unsupported HTTP version from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED);
        } catch (MethodNotSupportedException& e) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received request with unsupported method from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_METHOD_NOT_ALLOWED);
        } catch (MalformedException& e) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received malformed request from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_BAD_REQUEST);
        }
    }
}

void Server::serverThread() {
    while (true) {
        std::unique_lock lock(workerMutex);
        workerCV.wait(lock, [this] { return !requestsQueue.empty() || shouldStop; });
        lock.unlock();

        if (shouldStop) return;

        bool shouldContinue = true;

        while (shouldContinue && !shouldStop) {
            std::unique_lock queueLock(requestsQueueMutex);

            if (requestsQueue.empty()) {
                shouldContinue = false;
                continue;
            }

            auto request = std::move(requestsQueue.front());
            requestsQueue.pop();

            shouldContinue = !requestsQueue.empty();
            queueLock.unlock();

            std::function<Response(std::shared_ptr<Logger::Logger>, Request,
                                         sock::IPv4Addr, bool&, bool&, std::function<uint32_t(std::function<void()>)>,
                                         std::function<void(uint32_t)>)> handler = nullptr;

            std::unique_lock routesLock(routesMutex);
            if (request.second.hasHeader("host") && routes.find(request.second.getHeader("host")[0]) != routes.end()
            && routes[request.second.getHeader("host")[0]].find(request.second.getPath()) != routes[request.second.getHeader("host")[0]].end()) {
                handler = routes[request.second.getHeader("host")[0]][request.second.getPath()];
            }

            routesLock.unlock();

            std::unique_lock clientsLock(clientsMutex);
            sock::IPv4Addr clientDir = clients[request.first];
            clientsLock.unlock();

            if (handler == nullptr) {
                logger->log(Logger::level::INFO, Logger::group::NETWORK,
                            "Received request for unknown route from " + std::to_string(request.first) + ", closing connection");
                sendError(request.first, HTTP_STATUS_NOT_FOUND, request.second, clientDir);
            } else {
                try {
                    bool shouldClose = false;
                    auto response = handler(logger, request.second, clientDir, shouldStop, shouldClose,
                                            [this] (std::function<void()> func) { return registerCloseCall(std::move(func)); },
                                            [this] (uint32_t id) { return unregisterCloseCall(id); });

                    // The handler may take a long time to execute, so we need to check if the socket is still open
                    if (!socketMgr->isClosed(request.first)) {
                        socketMgr->send(request.first, std::move(response.serialize()));
                        if (shouldClose) socketMgr->close(request.first);
                    }
                } catch (std::exception& e) {
                    logger->log(Logger::level::FAILURE, Logger::group::NETWORK, "Error while handling request: " + std::string(e.what()));
                    sendError(request.first, HTTP_STATUS_INTERNAL_SERVER_ERROR, request.second, clientDir);
                }
            }
        }
    }
}

Response Server::getError(Version version, int status) {
    Response response(version, status);

    response.setHeader("Content-Type", "text/html");
    response.setHeader("Connection", "close");

    std::string statusString = std::to_string(status) + " " + STATUS_CODE_MSG.at(status);

    std::string body = "<!DOCTYPE html><html><head><title>" + statusString +
                       "</title></head><body><h1>" + statusString + "</h1></body></html>";
    std::vector<uint8_t> bodyVec(body.begin(), body.end());
    response.setBody(bodyVec);

    return response;
}

void Server::sendError(uint32_t sockId, int status, const Request& request, sock::IPv4Addr client) {
    std::unique_lock lock(errorPagesMutex);
    auto response = (!request.hasHeader("host")
            || errorPages.find(request.getHeader("host")[0]) == errorPages.end()) ? getError(request.getVersion(), status) :
            errorPages[request.getHeader("host")[0]](logger, request, client, status);
    socketMgr->send(sockId, std::move(response.serialize()));
    socketMgr->close(sockId);
}

void Server::sendError(uint32_t sockId, int status) {
    Request req("", Method::M_GET, Version::HTTP_1_1);
    sock::IPv4Addr client{};
    sendError(sockId, status, req, client);
}

void Server::stop() {
    if (shouldStop) return;

    logger->log(Logger::level::INFO, Logger::group::NETWORK, "Stopping HTTP server");

    socketMgr->close(mainSocketID, true);

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

    std::unique_lock clientsLock(clientsMutex);
    for (auto& client : clients) {
        socketMgr->close(client.first, true);
    }
    clients.clear();
    buffers.clear();

    std::unique_lock requestsQueueLock(requestsQueueMutex);
    while (!requestsQueue.empty()) requestsQueue.pop();

    mainSocket = nullptr;
}

uint32_t Server::registerCloseCall(std::function<void()> closeFunc) {
    if (shouldStop) return 0;
    std::unique_lock lock(closeCallsMutex);
    uint32_t id = closeCallID++;
    closeCalls[id] = std::move(closeFunc);
    return id;
}

void Server::unregisterCloseCall(uint32_t id) {
    std::unique_lock lock(closeCallsMutex);
    closeCalls.erase(id);
}

void Server::registerRoute(const std::string& host, const std::string& path, std::function<Response(
        std::shared_ptr<Logger::Logger>, Request, sock::IPv4Addr, bool&, bool&,
        std::function<uint32_t(std::function<void()>)>, std::function<void(uint32_t)>)> func) {
    std::unique_lock lock(routesMutex);
    if (routes.find(host) == routes.end()) {
        routes[host] = std::unordered_map<std::string, std::function<Response(
                std::shared_ptr<Logger::Logger>, Request, sock::IPv4Addr, bool&, bool&,
                std::function<uint32_t(std::function<void()>)>, std::function<void(uint32_t)>)>>();
    }

    routes[host][path] = std::move(func);
}

void Server::unregisterHost(const std::string& host) {
    std::unique_lock lock(routesMutex);
    routes.erase(host);
}

void Server::registerErrorPage(const std::string &host, std::function<Response(
        std::shared_ptr<Logger::Logger>, Request, sock::IPv4Addr, int)> func) {
    std::unique_lock lock(errorPagesMutex);
    errorPages[host] = std::move(func);
}

} // namespace http