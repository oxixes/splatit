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
            switch(request->getMethod()) {
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
                                                                      ipAndPort + " " + method + " " + request->getPath());

            std::unique_lock lock(*queueMutex);
            requestsQueue.emplace(sockId, std::move(request));
            queueCV->notify_one();
            lock.unlock();

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
        std::unique_lock lock(*queueMutex);
        queueCV->wait(lock, [this] { return !requestsQueue.empty() || !promisesQueue->empty() || shouldStop; });

        if (shouldStop) return;

        while (!promisesQueue->empty() && !shouldStop) {
            auto promise = std::move(promisesQueue->front());
            promisesQueue->pop();

            lock.unlock();
            try {
                promise->resolve();
            } catch (const std::exception& e) {
                if (promise->hasContext()) {
                    auto context = std::move(promise->getContext<std::pair<uint32_t, std::shared_ptr<Request>>>());
                    std::unique_lock clientsLock(clientsMutex);
                    sock::IPv4Addr clientDir = clients[context.first];
                    clientsLock.unlock();
                    sendError(context.first, HTTP_STATUS_INTERNAL_SERVER_ERROR, context.second, clientDir);
                }

                logger->log(Logger::level::FAILURE, Logger::group::NETWORK, "An exception occurred while resolving a promise: " +
                                                                            std::string(e.what()));
            }
            lock.lock();
        }

        if (shouldStop) break;

        while (!requestsQueue.empty() && !shouldStop) {
            auto request = std::move(requestsQueue.front());
            requestsQueue.pop();
            lock.unlock();

            std::function<void(Server*, std::unique_ptr<Context>)> handler = nullptr;

            std::unique_lock routesLock(routesMutex);
            if (request.second->hasHeader("host")) {
                if (routes.find(request.second->getHeader("host")[0]) != routes.end()
                    && routes[request.second->getHeader("host")[0]].find(request.second->getPath()) != routes[request.second->getHeader("host")[0]].end()) {
                    handler = routes[request.second->getHeader("host")[0]][request.second->getPath()];
                } else if (regexRoutes.find(request.second->getHeader("host")[0]) != regexRoutes.end()) {
                    for (auto& route : regexRoutes[request.second->getHeader("host")[0]]) {
                        if (std::regex_match(request.second->getPath(), route.first)) {
                            handler = route.second;
                            break;
                        }
                    }
                }
            }

            routesLock.unlock();

            std::unique_lock clientsLock(clientsMutex);
            sock::IPv4Addr clientDir = clients[request.first];
            clientsLock.unlock();

            if (handler == nullptr) {
                logger->log(Logger::level::INFO, Logger::group::NETWORK,
                            "Received request for unknown route from " + std::to_string(request.first) + ", closing connection");

                // Log the request for debugging purposes
                std::string body(request.second->getBody().begin(), request.second->getBody().end());
                logger->log(Logger::level::DEBUG, Logger::group::NETWORK, "Body: " + body);

                sendError(request.first, HTTP_STATUS_NOT_FOUND, request.second, clientDir);
            } else {
                try {
                    std::unique_ptr<Context> context = std::make_unique<Context>(logger, clientDir, request.first, std::move(request.second), 0,
                                                                                 promisesQueue, queueMutex, queueCV);
                    handler(this, std::move(context));
                } catch (std::exception& e) {
                    logger->log(Logger::level::FAILURE, Logger::group::NETWORK, "Error while handling request: " + std::string(e.what()));
                    sendError(request.first, HTTP_STATUS_INTERNAL_SERVER_ERROR, request.second, clientDir);
                }
            }

            lock.lock();
        }
    }
}

std::unique_ptr<Response> Server::getError(Version version, int status) {
    std::unique_ptr<Response> response = std::make_unique<Response>(version, status);

    response->setHeader("Content-Type", "text/html");
    if (version == Version::HTTP_1_1) response->setHeader("Connection", "close");

    std::string statusString = std::to_string(status) + " " + STATUS_CODE_MSG.at(status);

    std::string body = "<!DOCTYPE html><html><head><title>" + statusString +
                       "</title></head><body><h1>" + statusString + "</h1></body></html>";
    std::vector<uint8_t> bodyVec(body.begin(), body.end());
    response->setBody(std::move(bodyVec));

    return std::move(response);
}

void Server::sendError(uint32_t sockId, int status, const std::shared_ptr<Request>& request, sock::IPv4Addr client) {
    std::unique_lock lock(routesMutex);

    if (!request->hasHeader("host") || errorPages.find(request->getHeader("host")[0]) == errorPages.end()) {
        auto response = std::move(getError(request->getVersion(), status));
        socketMgr->send(sockId, std::move(response->serialize()));
        socketMgr->close(sockId);
    } else {
        std::unique_ptr<Context> context = std::make_unique<Context>(logger, client, sockId, request, status,
                                                                     promisesQueue, queueMutex, queueCV);
        lock.unlock();
        errorPages[request->getHeader("host")[0]](this, std::move(context));
    }
}

void Server::sendError(uint32_t sockId, int status) {
    std::shared_ptr<Request> req = std::make_shared<Request>("", Method::M_GET, Version::HTTP_1_1);
    sock::IPv4Addr client{};
    sendError(sockId, status, req, client);
}

void Server::stop() {
    if (shouldStop) return;

    logger->log(Logger::level::INFO, Logger::group::NETWORK, "Stopping HTTP server");

    socketMgr->close(mainSocketID, true);

    shouldStop = true;

    queueCV->notify_all();
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

    std::unique_lock requestsQueueLock(*queueMutex);
    requestsQueue = std::queue<std::pair<uint32_t, std::shared_ptr<Request>>>();
    promisesQueue = std::make_shared<std::queue<std::shared_ptr<Promise>>>();

    mainSocket = nullptr;
}

void Server::registerRoute(const std::string& host, const std::string& path, std::function<void(
        Server*, std::shared_ptr<Context>)> func) {
    std::unique_lock lock(routesMutex);
    if (routes.find(host) == routes.end()) {
        routes[host] = std::unordered_map<std::string, std::function<void(Server*, std::shared_ptr<Context>)>>();
    }

    routes[host][path] = std::move(func);
}

void Server::registerRegexRoute(const std::string& host, const std::string& path, std::function<void(
        Server*, std::shared_ptr<Context>)> func) {
    std::unique_lock lock(routesMutex);

    std::regex regexPath(path);

    if (regexRoutes.find(host) == regexRoutes.end()) {
        regexRoutes[host] = std::vector<std::pair<std::regex, std::function<void(Server*, std::shared_ptr<Context>)>>>();
    }

    regexRoutes[host].emplace_back(std::move(regexPath), std::move(func));
}

void Server::unregisterHost(const std::string& host) {
    std::unique_lock lock(routesMutex);
    routes.erase(host);
}

void Server::registerErrorPage(const std::string& host, std::function<void(
        Server*, std::shared_ptr<Context>)> func) {
    std::unique_lock lock(routesMutex);
    errorPages[host] = std::move(func);
}

void Server::sendResponse(std::shared_ptr<Context> context, std::unique_ptr<Response> response, bool keepAlive) {
    socketMgr->send(context->clientSockId, std::move(response->serialize()));
    if (!keepAlive) socketMgr->close(context->clientSockId);
}

} // namespace http