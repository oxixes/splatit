#include "socketManager.hpp"

#include <chrono>

#include "../exceptions.hpp"
#include "../util/util.hpp"

SocketManager::SocketManager(std::shared_ptr<Logger::Logger> logger) : logger(std::move(logger)) {}

uint32_t SocketManager::addTCPSocket(std::shared_ptr<sock::TCPSocket> socket,
                                         std::function<void(uint32_t, uint32_t, sock::IPv4Addr)> acceptCallback,
                                         std::function<void(uint32_t)> closeCallback,
                                         std::function<void(uint32_t, std::vector<uint8_t>)> connRecvCallback,
                                         std::function<void(uint32_t)> connCloseCallback, int keepAliveTimeout) {
    uint32_t socketId = nextSocketId++;

    std::unique_lock socketsLock(socketsMutex);
    sockets.insert(std::make_pair(socketId, SocketInfo(
        std::move(socket),
        SocketType::TCP,
        std::move(acceptCallback),
        nullptr,
        std::move(connRecvCallback),
        nullptr,
        std::make_pair(std::move(closeCallback), std::move(connCloseCallback)),
        keepAliveTimeout,
        -1
    )));

    return socketId;
}

uint32_t SocketManager::addTCPSocketConn(std::shared_ptr<sock::TCPSocket> socket,
                                             std::function<void(uint32_t)> connectCallback,
                                             std::function<void(uint32_t, std::vector<uint8_t>)> recvCallback,
                                             std::function<void(uint32_t)> closeCallback, int keepAliveTimeout) {
    uint32_t socketId = nextSocketId++;

    int64_t keepAlive = -1;
    if (keepAliveTimeout > 0) {
        keepAlive = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() + (keepAliveTimeout * 1000);
    }

    std::unique_lock socketsLock(socketsMutex);
    sockets.insert(std::make_pair(socketId, SocketInfo(
        std::move(socket),
        SocketType::TCP_CONN,
        nullptr,
        std::move(connectCallback),
        std::move(recvCallback),
        nullptr,
        std::make_pair(std::move(closeCallback), nullptr),
        keepAlive,
        -1
    )));

    return socketId;
}

uint32_t SocketManager::addUDPSocket(std::shared_ptr<sock::UDPSocket> socket,
                          std::function<void(uint32_t, std::vector<uint8_t>, sock::IPv4Addr)> recvCallback,
                          std::function<void(uint32_t)> closeCallback) {
    uint32_t socketId = nextSocketId++;

    std::unique_lock socketsLock(socketsMutex);
    sockets.insert(std::make_pair(socketId, SocketInfo(
        std::move(socket),
        SocketType::UDP,
        nullptr,
        nullptr,
        nullptr,
        std::move(recvCallback),
        std::make_pair(std::move(closeCallback), nullptr),
        -1,
        -1
    )));

    return socketId;
}

uint64_t SocketManager::process(uint64_t ms) {
    std::unique_lock socketsLock(socketsMutex);
    if (sockets.empty()) return UINT64_MAX;

    std::vector<pollfd> fds;
    std::vector<uint32_t> socketIndexToId;

    for (auto& socket : sockets) {
        short events = POLLIN;
        if (socket.second.type == SocketType::TCP_CONN || socket.second.type == SocketType::UDP) {
            // If the type is TCP_CONN or UDP, it is guaranteed to be in the sendBuffers map
            if (socket.second.socket->getLastResult() == sock::ResultType::NEEDS_WRITE) {
                events |= POLLOUT;
            }
        }

        socketIndexToId.push_back(socket.first);
        fds.push_back({socket.second.socket->getSocket(), events, 0});
    }
    socketsLock.unlock();

    int timeToWait = (ms == UINT64_MAX) ? -1 : (int) ms;
#ifdef _WIN32
    int ret = WSAPoll(fds.data(), fds.size(), timeToWait);
#else
    int ret = poll(fds.data(), fds.size(), timeToWait);
#endif

#ifdef _WIN32
    if (ret == SOCKET_ERROR) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Error polling sockets: " + util::getWSAError(WSAGetLastError()));
    }
#else
    if (ret == -1) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK, "Error polling sockets: " + std::to_string(strerror(errno)));
    }
#endif

    if (ret > 0) {
        int i = 0;
        for (auto fd : fds) {
            auto socketInfoIt = sockets.find(socketIndexToId[i]);
            if (socketInfoIt == sockets.end()) {
                i++;
                continue;
            }

            auto socketInfo = &socketInfoIt->second;

            if (fd.revents & POLLHUP || fd.revents & POLLERR) {
                logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                            "Socket with ID " + std::to_string(socketIndexToId[i]) + " has been closed (socket error or disconnection)");
                close(socketIndexToId[i], true);
                i++;
                continue;
            }

            if (fd.revents & POLLOUT) {
                socketsLock.lock();

                bool locked = true;
                if (sockets[socketIndexToId[i]].type == SocketType::TCP_CONN) {
                    sock::SocketStatus status = sockets[socketIndexToId[i]].socket->getStatus();
                    if (status == sock::SocketStatus::CONNECTING) {
                        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketIndexToId[i]].socket);
                        try {
                            socket->connect();
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Socket with ID " + std::to_string(socketIndexToId[i]) + " has connected");

                            if (socketInfo->connectCallback != nullptr) {
                                auto connectCallback = socketInfo->connectCallback;
                                socketsLock.unlock();
                                locked = false;

                                connectCallback(socketIndexToId[i]);
                            }
                        } catch (const sock::RetryableException &e) {
                            i++;
                            socketsLock.unlock();
                            continue;
                        } catch (const sock::FatalException &e) {
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Error connecting from socket with ID " + std::to_string(socketIndexToId[i]) +
                                        ": " + e.what());
                            close(socketIndexToId[i], true);
                            i++;

                            socketsLock.unlock();
                            continue;
                        }
                    } else if (status == sock::SocketStatus::CONNECTED) {
                        socketsLock.unlock();
                        locked = false;

                        send(socketIndexToId[i]);
                    } else if (status == sock::SocketStatus::CLOSING) {
                        socketsLock.unlock();
                        locked = false;

                        close(socketIndexToId[i]);
                    }
                } else if (socketInfo->type == SocketType::UDP) {
                    socketsLock.unlock();
                    locked = false;

                    sendto(socketIndexToId[i]);
                }

                if (locked) socketsLock.unlock();
            }

            if (fd.revents & POLLIN) {
                socketsLock.lock();
                bool locked = true;

                SocketType type = socketInfo->type;
                if (type == SocketType::TCP) {
                    accept(socketIndexToId[i]);
                } else if (type == SocketType::TCP_CONN) {
                    sock::SocketStatus status = socketInfo->socket->getStatus();
                    if (status == sock::SocketStatus::CONNECTING) {
                        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(socketInfo->socket);
                        try {
                            socket->connect();
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Socket with ID " + std::to_string(socketIndexToId[i]) + " has connected");

                            if (socketInfo->connectCallback != nullptr) {
                                auto connectCallback = socketInfo->connectCallback;
                                socketsLock.unlock();
                                locked = false;

                                connectCallback(socketIndexToId[i]);
                            }
                        } catch (const sock::RetryableException &e) {
                            i++;

                            socketsLock.unlock();
                            continue;
                        } catch (const sock::FatalException &e) {
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Error connecting from socket with ID " + std::to_string(socketIndexToId[i]) +
                                        ": " + e.what());
                            close(socketIndexToId[i], true);
                            i++;

                            socketsLock.unlock();
                            continue;
                        }
                    } else if (status == sock::SocketStatus::CONNECTED) {
                        socketsLock.unlock();
                        locked = false;

                        recv(socketIndexToId[i]);
                    } else if (status == sock::SocketStatus::CLOSING) {
                        socketsLock.unlock();
                        locked = false;

                        close(socketIndexToId[i]);
                    }
                } else if (type == SocketType::UDP) {
                    socketsLock.unlock();
                    locked = false;

                    recvfrom(socketIndexToId[i]);
                }

                if (locked) socketsLock.unlock();
            }

            i++;
        }
    }

    uint64_t nextTimeout = UINT64_MAX;

    // Check for keep alive timeouts
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    socketsLock.lock();
    for (auto& socket : sockets) {
        // Check for keep alive timeouts
        if (socket.second.keepAliveTimeout > 0 && socket.second.keepAliveTimeout < now) {
            if (socket.second.type == SocketType::TCP_CONN) {
                logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                            "Socket with ID " + std::to_string(socket.first) + " has exceeded its keep alive timeout, will be closed");
                close(socket.first);
            }
        } else if (socket.second.keepAliveTimeout > 0 && socket.second.keepAliveTimeout - now < nextTimeout) {
            nextTimeout = socket.second.keepAliveTimeout - now;
        }
    }

    // Check for previously not closed sockets because of data in the send buffer and sockets that haven't been closed
    // because we were waiting for a response to the close request
    std::unique_lock closeQueueLock(closeQueueMutex);

    for (auto socketId = closeQueue.begin(); socketId != closeQueue.end(); ) {
        if (sockets.find(*socketId) == sockets.end()) {
            socketId = closeQueue.erase(socketId);
            continue;
        }

        auto socketInfo = &sockets[*socketId];
        if (socketInfo->type == SocketType::TCP_CONN || socketInfo->type == SocketType::UDP) {
            bool hasData = false;
            if (socketInfo->type == SocketType::TCP_CONN) {
                hasData = !socketInfo->tcpSendBuffer.empty();
            } else if (socketInfo->type == SocketType::UDP) {
                hasData = !socketInfo->udpSendBuffer.empty();
            }

            if (!hasData && socketInfo->closeTimeout <= 0) {
                if (close(*socketId)) {
                    socketId = closeQueue.erase(socketId);
                    continue;
                }
            } else if (socketInfo->closeTimeout > 0) {
                if (socketInfo->closeTimeout < now) {
                    if (close(*socketId, true)) {
                        socketId = closeQueue.erase(socketId);
                        continue;
                    }
                } else if (socketInfo->closeTimeout - now < nextTimeout) {
                    nextTimeout = socketInfo->closeTimeout - now;
                }
            }
        }

        socketId++;
    }

    return nextTimeout;
}

void SocketManager::connect(uint32_t socketId, sock::IPv4Addr address) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::TCP_CONN) return;

    auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(socketInfo->socket);
    try {
        struct sockaddr_in addr = util::ipv4ToSockAddr(address);
        socket->connect((struct sockaddr*) &addr, sizeof(addr));
    } catch (const sock::RetryableException& e) {
        return;
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Error connecting from socket with ID " + std::to_string(socketId) + ": " + e.what());
        close(socketId, true);
        return;
    }
}

bool SocketManager::send(uint32_t socketId, std::vector<uint8_t> data) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return false;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::TCP_CONN) return false;

    size_t total = 0;
    while (total < data.size()) {
        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(socketInfo->socket);
        size_t sent;
        try {
            sent = socket->send(data.data() + total, data.size() - total, 0);
        } catch (const sock::RetryableException& e) {
            socketInfo->tcpSendBuffer.insert(socketInfo->tcpSendBuffer.end(),
                                             data.begin() + (unsigned int) total, data.end());
            break;
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
            close(socketId, true);
            return false;
        }

        total += sent;
    }

    return true;
}

void SocketManager::send(uint32_t socketId) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::TCP_CONN) return;

    if (socketInfo->tcpSendBuffer.empty()) return;

    size_t oldSize = socketInfo->tcpSendBuffer.size();

    if (!send(socketId, socketInfo->tcpSendBuffer)) {
        socketInfo->tcpSendBuffer.erase(socketInfo->tcpSendBuffer.begin(),
                                        socketInfo->tcpSendBuffer.begin() + (ssize_t) oldSize);
    }
}

bool SocketManager::sendto(uint32_t socketId, std::vector<uint8_t> data, sock::IPv4Addr address) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return false;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::UDP) return false;

    auto socket = std::dynamic_pointer_cast<sock::UDPSocket>(socketInfo->socket);

    struct sockaddr_in addr = util::ipv4ToSockAddr(address);

    try {
        socket->sendto(data.data(), data.size(), 0, (struct sockaddr*) &addr, sizeof(addr));
    } catch (const sock::RetryableException& e) {
        socketInfo->udpSendBuffer.emplace_back(address, std::move(data));
        return true;
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        close(socketId, true);
        return false;
    }

    return true;
}

void SocketManager::sendto(uint32_t socketId) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::UDP) return;

    for (auto it_packets = socketInfo->udpSendBuffer.begin(); it_packets != socketInfo->udpSendBuffer.end(); it_packets++) {
        if (!sendto(socketId, it_packets->second, it_packets->first)) break;
        it_packets = socketInfo->udpSendBuffer.erase(it_packets);
    }
}

void SocketManager::recv(uint32_t socketId) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::TCP_CONN) return;

    std::vector<uint8_t> recvBuf;
    recvBuf.resize(1024 * 16); // 16k should be enough for most (if not all) packets
    auto tcpSocket = std::dynamic_pointer_cast<sock::TCPSocket>(socketInfo->socket);
    try {
        int recvBytes = tcpSocket->recv(recvBuf.data(), recvBuf.capacity(), 0);
        if (recvBytes == 0) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                        "Socket with ID " + std::to_string(socketId) + " has been closed (read 0 bytes)");
            close(socketId, true);
            return;
        } else if (std::find(closeQueue.begin(), closeQueue.end(), socketId) != closeQueue.end()) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                        "Socket with ID " + std::to_string(socketId) + " has received some data"
                                                                                 " but is in the close queue, so it will be ignored.");
            return;
        }

        recvBuf.resize(recvBytes);

        if (socketInfo->tcpRecvCallback != nullptr) {
            auto tcpRecvCallback = socketInfo->tcpRecvCallback;
            socketsLock.unlock();

            tcpRecvCallback(socketId, std::move(recvBuf));
        }
    } catch (const sock::RetryableException& e) {
        // Actually, this could happen, it will be already handled by the poll call
//        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
//                    "Socket with ID " + std::to_string(socketId) + " threw a retryable exception,"
//                                                                             " this should NOT happen, but will be ignored.");
        return;
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Socket with ID " + std::to_string(socketId) + " has been closed (read threw a fatal exception)");
        close(socketId, true);
        return;
    }
}

void SocketManager::recvfrom(uint32_t socketId) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::UDP) return;

    std::vector<uint8_t> recvBuf;
    recvBuf.resize(65535); // A UDP won't receive more than 65535 bytes (it's actually 65507, but we'll use 65535)

    struct sockaddr_in addr{};
    int addrLen = sizeof(addr);

    auto udpSocket = std::dynamic_pointer_cast<sock::UDPSocket>(socketInfo->socket);
    try {
        int recvBytes = udpSocket->recvfrom(recvBuf.data(), recvBuf.capacity(), 0, (sockaddr*) &addr, &addrLen);
        if (std::find(closeQueue.begin(), closeQueue.end(), socketId) != closeQueue.end()) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                        "Socket with ID " + std::to_string(socketId) + " has received some data"
                                                                                 " but is in the close queue, so it will be ignored.");
            return;
        }

        if (recvBytes == 0) return;

        recvBuf.resize(recvBytes);

        auto* ipv4addr = (uint8_t*) &addr.sin_addr.S_un.S_un_b;
        sock::IPv4Addr dir{ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3], ntohs(addr.sin_port)};

        if (socketInfo->udpRecvCallback != nullptr) {
            auto udpRecvCallback = socketInfo->udpRecvCallback;
            socketsLock.unlock();

            udpRecvCallback(socketId, std::move(recvBuf), dir);
        }
    } catch (const sock::RetryableException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Socket with ID " + std::to_string(socketId) + " threw a retryable exception,"
                                                                             " this should NOT happen, but will be ignored.");
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Socket with ID " + std::to_string(socketId) + " has been closed (read threw a fatal exception)");
        close(socketId, true);
    }
}

void SocketManager::accept(uint32_t socketId) {
    std::unique_lock socketsLock(socketsMutex);
    //bool socketsLocked = true;

    if (sockets.find(socketId) == sockets.end()) return;
    auto socketInfo = &sockets[socketId];
    if (socketInfo->type != SocketType::TCP) return;

    auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(socketInfo->socket);
    try {
        struct sockaddr addr{};
        int addrLen = sizeof(addr);
        auto newSocket = socket->accept(&addr, &addrLen);

        int keepAliveTimeout = (int) socketInfo->keepAliveTimeout;

        auto *addrIn = (struct sockaddr_in *) &addr;
        auto *ipv4addr = (uint8_t *) &addrIn->sin_addr.S_un.S_un_b;
        sock::IPv4Addr dir{ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3], addrIn->sin_port};

        std::function<void(uint32_t)> connectCallback;
        if (socketInfo->acceptCallback != nullptr) {
            connectCallback = [socketInfo, socketId, dir](uint32_t newSocketId) {
                socketInfo->acceptCallback(socketId, newSocketId, dir);
            };
        }

        uint32_t newSocketId = addTCPSocketConn(std::shared_ptr<sock::TCPSocket>(newSocket),
                                                    std::move(connectCallback),socketInfo->tcpRecvCallback,
                                                    socketInfo->closeCallback.second,
                                                    keepAliveTimeout);

        if (socketInfo->acceptCallback != nullptr && socket->getLastResult() == sock::ResultType::SUCCESS) {
            socketInfo->acceptCallback(socketId, newSocketId, dir);
        }

        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Socket with ID " + std::to_string(socketId) + " has accepted a new connection with ID " +
                    std::to_string(newSocketId));

        // if (acceptCallback != acceptCallbacks.end()) acceptCallback->second(socketId, newSocketId, dir);
    } catch (const sock::SSLException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        close(socketId, true);
    }
}

bool SocketManager::close(uint32_t socketId, bool force) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return false;
    auto socketInfo = &sockets[socketId];

    std::unique_lock closeQueueLock(closeQueueMutex);

    bool hasData = false;
    if (socketInfo->type == SocketType::TCP_CONN) {
        hasData = !socketInfo->tcpSendBuffer.empty();
    } else if (socketInfo->type == SocketType::UDP) {
        hasData = !socketInfo->udpSendBuffer.empty();
    }

    if (!force && hasData && std::find(closeQueue.begin(), closeQueue.end(), socketId) == closeQueue.end()) {
        closeQueue.push_back(socketId);
    } else {
        try {
            socketInfo->socket->close(force);
        } catch (const sock::RetryableException& e) {
            if (socketInfo->closeTimeout < 0) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                socketInfo->closeTimeout = now + CLOSE_TIMEOUT;
            }
            return false;
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        }

        if (socketInfo->closeCallback.first != nullptr) {
            auto closeCallback = socketInfo->closeCallback.first;
            sockets.erase(socketId);
            socketsLock.unlock();

            closeCallback(socketId);
        } else {
            sockets.erase(socketId);
        }

        return true;
    }

    return false;
}

bool SocketManager::isClosed(uint32_t socketId) {
    std::unique_lock socketsLock(socketsMutex);
    return sockets.find(socketId) == sockets.end();
}

void SocketManager::cleanup() {
    std::unique_lock socketsLock(socketsMutex);

    std::vector<uint32_t> socketIds(sockets.size());
    for (auto& socket : sockets) socketIds.push_back(socket.first);
    for (auto socketId : socketIds) close(socketId, true);

    std::unique_lock closeQueueLock(closeQueueMutex);
    closeQueue.clear();
}