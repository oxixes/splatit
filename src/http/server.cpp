#include "server.hpp"

#include <utility>
#include <grpcpp/support/server_callback.h>

#include "../exceptions.hpp"
#include "../socket/sslSocket.hpp"

namespace http {

Server::Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SocketManager> socketMgr,
               sock::IPv4Addr listenDir, int keepAliveTimeout, bool ssl, EVP_PKEY* key, X509* cert) {
    this->logger = std::move(logger);
    this->socketMgr = std::move(socketMgr);
    this->keepAliveTimeout = keepAliveTimeout;
    this->mainSocketID = 0;
    this->scheduler = std::make_shared<async::Scheduler>(queueCV);

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
    socket->bind(reinterpret_cast<struct sockaddr*>(&address), sizeof(address));

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
                case Method::M_OPTIONS:
                    method = "OPTIONS";
                    break;
            }

            logger->log(Logger::level::DEBUG, Logger::group::NETWORK, "Received request from " +
                                                                      ipAndPort + " " + method + " " + request->getPath());

            std::unique_lock lock(*queueMutex);
            requestsQueue.emplace(sockId, std::move(request));
            queueCV->notify_one();
            lock.unlock();

            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(length));
            finish = false;
        } catch (NotCompleteException& _) {
            // Do nothing, wait for more data
        } catch (LengthUnknownException& _) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received request with unknown length from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_BAD_REQUEST);
        } catch (VersionNotSupportedException& _) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received request with unsupported HTTP version from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED);
        } catch (MethodNotSupportedException& _) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received request with unsupported method from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_METHOD_NOT_ALLOWED);
        } catch (MalformedException& _) {
            logger->log(Logger::level::WARN, Logger::group::NETWORK,
                        "Received malformed request from " + std::to_string(sockId) + ", closing connection");
            sendError(sockId, HTTP_STATUS_BAD_REQUEST);
        }
    }
}

void Server::serverThread() {
    while (true) {
        std::unique_lock lock(*queueMutex);
        queueCV->wait(lock, [this] { return !requestsQueue.empty() || scheduler->hasTasks() || shouldStop; });

        if (shouldStop) return;

        lock.unlock();
        while (scheduler->hasTasks() && !shouldStop) {
            auto task = scheduler->getTask();
            if (!task) break;

            try {
                async::Scheduler::run(task);
            } catch (const std::exception& e) {
                if (task->getContext().has_value()) {
                    if (task->getContext().type() != typeid(std::pair<uint32_t, std::shared_ptr<Request>>)) {
                        logger->log(Logger::level::FAILURE, Logger::group::NETWORK, "An exception occurred while processing a gRPC request: " +
                                                                                         std::string(e.what()));
                        const auto reactor = std::any_cast<grpc::ServerUnaryReactor*>(task->getContext());
                        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Internal server error"));
                    } else {
                        auto context = std::move(std::any_cast<std::pair<uint32_t, std::shared_ptr<Request>>>(task->getContext()));
                        std::unique_lock clientsLock(clientsMutex);
                        sock::IPv4Addr clientDir = clients[context.first];
                        clientsLock.unlock();
                        sendError(context.first, HTTP_STATUS_INTERNAL_SERVER_ERROR, context.second, clientDir);
                    }
                }

                logger->log(Logger::level::FAILURE, Logger::group::NETWORK, "An exception occurred while running a request handler: " +
                                                                            std::string(e.what()));
            }
        }
        lock.lock();

        if (shouldStop) break;

        while (!requestsQueue.empty() && !shouldStop) {
            auto request = std::move(requestsQueue.front());
            requestsQueue.pop();
            lock.unlock();

            std::function<async::Task<void>(Server*, std::unique_ptr<Context>)> handler = nullptr;

            std::unique_lock routesLock(routesMutex);
            if (request.second->hasHeader("host")) {
                if (routes.contains(request.second->getHeader("host")[0])
                    && routes[request.second->getHeader("host")[0]].contains(request.second->getPath())) {
                    handler = routes[request.second->getHeader("host")[0]][request.second->getPath()];
                } else if (regexRoutes.contains(request.second->getHeader("host")[0])) {
                    for (auto& route : regexRoutes[request.second->getHeader("host")[0]]) {
                        if (std::regex_match(request.second->getPath(), route.first)) {
                            handler = route.second;
                            break;
                        }
                    }
                }
            }

            if (handler == nullptr && routes.contains("*")
                && routes["*"].contains(request.second->getPath())) {
                handler = routes["*"][request.second->getPath()];
            } else if (handler == nullptr && regexRoutes.contains("*")) {
                for (auto& route : regexRoutes["*"]) {
                    if (std::regex_match(request.second->getPath(), route.first)) {
                        handler = route.second;
                        break;
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
                    std::unique_ptr<Context> context = std::make_unique<Context>(logger, clientDir, request.first, request.second, 0,
                                                                                 scheduler);
                    auto task = std::move(handler(this, std::move(context)));
                    task.setScheduler(scheduler);
                    task.setContext(std::make_pair(request.first, request.second));
                    scheduler->schedule(std::move(task));
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

    if (!request->hasHeader("host") || !errorPages.contains(request->getHeader("host")[0])) {
        if (errorPages.contains("*")) {
            lock.unlock();
            std::unique_ptr<Context> context = std::make_unique<Context>(logger, client, sockId, request, status,
                                                                         scheduler);
            auto task = std::move(errorPages["*"](this, std::move(context)));
            task.setScheduler(scheduler);
            task.setContext(std::make_pair(sockId, request));
            scheduler->schedule(std::move(task));
            return;
        }

        auto response = std::move(getError(request->getVersion(), status));
        socketMgr->send(sockId, std::move(response->serialize()));
        socketMgr->close(sockId);
    } else {
        std::unique_ptr<Context> context = std::make_unique<Context>(logger, client, sockId, request, status,
                                                                     scheduler);
        lock.unlock();
        auto task = std::move(errorPages[request->getHeader("host")[0]](this, std::move(context)));
        task.setScheduler(scheduler);
        task.setContext(std::make_pair(sockId, request));
        scheduler->schedule(std::move(task));
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
    scheduler->clear();

    mainSocket = nullptr;
}

void Server::registerRoute(const std::string& host, const std::string& path, std::function<async::Task<void>(
        Server*, std::shared_ptr<Context>)> func) {
    std::unique_lock lock(routesMutex);
    if (!routes.contains(host)) {
        routes[host] = std::unordered_map<std::string, std::function<async::Task<void>(Server*, std::shared_ptr<Context>)>>();
    }

    routes[host][path] = std::move(func);
}

void Server::registerRegexRoute(const std::string& host, const std::string& path, std::function<async::Task<void>(
        Server*, std::shared_ptr<Context>)> func) {
    std::unique_lock lock(routesMutex);

    std::regex regexPath(path);

    if (!regexRoutes.contains(host)) {
        regexRoutes[host] = std::vector<std::pair<std::regex, std::function<async::Task<void>(Server*, std::shared_ptr<Context>)>>>();
    }

    regexRoutes[host].emplace_back(std::move(regexPath), std::move(func));
}

void Server::unregisterHost(const std::string& host) {
    std::unique_lock lock(routesMutex);
    routes.erase(host);
}

void Server::registerErrorPage(const std::string& host, std::function<async::Task<void>(
        Server*, std::shared_ptr<Context>)> func) {
    std::unique_lock lock(routesMutex);
    errorPages[host] = std::move(func);
}

void Server::sendResponse(std::shared_ptr<Context> context, std::unique_ptr<Response> response, bool keepAlive) const {
    socketMgr->send(context->clientSockId, std::move(response->serialize()));
    if (!keepAlive) socketMgr->close(context->clientSockId);
}

void Server::scheduleArbitraryFunction(async::Task<void>&& task) const {
    task.setScheduler(scheduler);
    scheduler->schedule(std::move(task));
}

} // namespace http