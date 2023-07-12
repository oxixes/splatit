#include "server.hpp"

#include <utility>

HTTP_Server::HTTP_Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SocketManager> socketMgr,
                         std::shared_ptr<db::Database> db, sock::IPv4Dir listenDir, int keepAliveTimeout,
                         EVP_PKEY* key, X509* cert) {
    this->logger = std::move(logger);
    this->socketMgr = std::move(socketMgr);
    this->db = std::move(db);
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
    logger->log(Logger::level::INFO, Logger::group::NETWORK,
                "Starting HTTP server with " + std::to_string(workerCount) + " workers");

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
        logger->log(Logger::level::WARN, Logger::group::NETWORK,
                    "Received too much data from " + std::to_string(sockId) + ", closing connection");
        sendError(sockId, HTTP_STATUS_PAYLOAD_TOO_LARGE);
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
        logger->log(Logger::level::WARN, Logger::group::NETWORK,
                    "Received request with unknown length from " + std::to_string(sockId) + ", closing connection");
        sendError(sockId, HTTP_STATUS_BAD_REQUEST);
    } catch (http::VersionNotSupportedException& e) {
        logger->log(Logger::level::WARN, Logger::group::NETWORK,
                    "Received request with unsupported HTTP version from " + std::to_string(sockId) + ", closing connection");
        sendError(sockId, HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED);
    } catch (http::MethodNotSupportedException& e) {
        logger->log(Logger::level::WARN, Logger::group::NETWORK,
                    "Received request with unsupported method from " + std::to_string(sockId) + ", closing connection");
        sendError(sockId, HTTP_STATUS_METHOD_NOT_ALLOWED);
    } catch (http::MalformedException& e) {
        logger->log(Logger::level::WARN, Logger::group::NETWORK,
                    "Received malformed request from " + std::to_string(sockId) + ", closing connection");
        sendError(sockId, HTTP_STATUS_BAD_REQUEST);
    }
}

void HTTP_Server::serverThread() {
    while (true) {
        std::unique_lock lock(workerMutex);
        workerCV.wait(lock, [this] { return !requestsQueue.empty() || shouldStop; });
        lock.unlock();

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

            std::function<http::Response( std::shared_ptr<Logger::Logger>, std::shared_ptr<db::Database>, http::Request,
                    sock::IPv4Dir, bool&, bool&, std::function<unsigned int(std::function<void()>)>,
                    std::function<void(unsigned int)>)> handler = nullptr;

            std::unique_lock routesLock(routesMutex);
            if (request.second.hasHeader("host") && routes.find(request.second.getHeader("host")[0]) != routes.end()
            && routes[request.second.getHeader("host")[0]].find(request.second.getPath()) != routes[request.second.getHeader("host")[0]].end()) {
                handler = routes[request.second.getHeader("host")[0]][request.second.getPath()];
            }

            routesLock.unlock();

            std::unique_lock clientsLock(clientsMutex);
            sock::IPv4Dir clientDir = clients[request.first];
            clientsLock.unlock();

            if (handler == nullptr) {
                logger->log(Logger::level::INFO, Logger::group::NETWORK,
                            "Received request for unknown route from " + std::to_string(request.first) + ", closing connection");
                sendError(request.first, HTTP_STATUS_NOT_FOUND, request.second, clientDir);
            } else {
                try {
                    bool shouldClose = false;
                    auto response = handler(logger, db, request.second, clientDir, shouldStop, shouldClose,
                                            [this] (std::function<void()> func) { return registerCloseCall(std::move(func)); },
                                            [this] (unsigned int id) { return unregisterCloseCall(id); });

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

void HTTP_Server::sendError(unsigned int sockId, int status, const http::Request& request, sock::IPv4Dir client) {
    std::unique_lock lock(errorPagesMutex);
    auto response = (!request.hasHeader("host")
            || errorPages.find(request.getHeader("host")[0]) == errorPages.end()) ? getError(request.getVersion(), status) :
            errorPages[request.getHeader("host")[0]](logger, request, client, status);
    socketMgr->send(sockId, std::move(response.serialize()));
    socketMgr->close(sockId);
}

void HTTP_Server::sendError(unsigned int sockId, int status) {
    http::Request req("", http::Method::M_GET, http::Version::HTTP_1_1);
    sock::IPv4Dir client{};
    sendError(sockId, status, req, client);
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

void HTTP_Server::registerRoute(const std::string& host, const std::string& path, std::function<http::Response(
        std::shared_ptr<Logger::Logger>, std::shared_ptr<db::Database>, http::Request, sock::IPv4Dir, bool&, bool&,
        std::function<unsigned int(std::function<void()>)>, std::function<void(unsigned int)>)> func) {
    std::unique_lock lock(routesMutex);
    if (routes.find(host) == routes.end()) {
        routes[host] = std::map<std::string, std::function<http::Response(
                std::shared_ptr<Logger::Logger>, std::shared_ptr<db::Database>, http::Request, sock::IPv4Dir, bool&, bool&,
                std::function<unsigned int(std::function<void()>)>, std::function<void(unsigned int)>)>>();
    }

    routes[host][path] = std::move(func);
}

void HTTP_Server::registerErrorPage(const std::string &host, std::function<http::Response(
        std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Dir, int)> func) {
    std::unique_lock lock(errorPagesMutex);
    errorPages[host] = std::move(func);
}