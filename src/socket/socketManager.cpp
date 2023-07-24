#include "socketManager.hpp"

#include <chrono>
#include "../util/util.hpp"

SocketManager::SocketManager(std::shared_ptr<Logger::Logger> logger) : logger(std::move(logger)) {}

unsigned int SocketManager::addTCPSocket(std::shared_ptr<sock::TCPSocket> socket,
                                         std::function<void(unsigned int, unsigned int, sock::IPv4Dir)> acceptCallback,
                                         std::function<void(unsigned int)> closeCallback,
                                         std::function<void(unsigned int, std::vector<uint8_t>)> connRecvCallback,
                                         std::function<void(unsigned int)> connCloseCallback, int keepAliveTimeout) {
    unsigned int socketId = nextSocketId++;

    std::unique_lock acceptCallbackLock(acceptCallbacksMutex);
    if (acceptCallback != nullptr) {
        acceptCallbacks.insert(std::make_pair(socketId, std::move(acceptCallback)));
    }
    acceptCallbackLock.unlock();

    std::unique_lock closeCallbackLock(closeCallbacksMutex);
    if (closeCallback != nullptr && connCloseCallback != nullptr) {
        closeCallbacks.insert(std::make_pair(socketId, std::make_pair(std::move(closeCallback),
                                                                      std::move(connCloseCallback))));
    }
    closeCallbackLock.unlock();

    std::unique_lock recvCallbackLock(tcpRecvCallbacksMutex);
    if (connRecvCallback != nullptr) {
        tcpRecvCallbacks.insert(std::make_pair(socketId, std::move(connRecvCallback)));
    }
    recvCallbackLock.unlock();

    std::unique_lock keepAliveTimeoutsLock(keepAliveTimeoutsMutex);
    if (keepAliveTimeout > 0) {
        keepAliveTimeouts.insert(std::make_pair(socketId, keepAliveTimeout));
    }
    keepAliveTimeoutsLock.unlock();

    std::unique_lock socketsLock(socketsMutex);
    sockets.insert(std::make_pair(socketId, std::make_pair(SocketType::TCP, std::move(socket))));
    return socketId;
}

unsigned int SocketManager::addTCPSocketConn(std::shared_ptr<sock::TCPSocket> socket,
                                             std::function<void(unsigned int)> connectCallback,
                                             std::function<void(unsigned int, std::vector<uint8_t>)> recvCallback,
                                             std::function<void(unsigned int)> closeCallback, int keepAliveTimeout) {
    unsigned int socketId = nextSocketId++;

    std::unique_lock connectCallbackLock(connectCallbacksMutex);
    if (connectCallback != nullptr) {
        connectCallbacks.insert(std::make_pair(socketId, std::move(connectCallback)));
    }
    connectCallbackLock.unlock();

    std::unique_lock recvCallbackLock(tcpRecvCallbacksMutex);
    if (recvCallback != nullptr) {
        tcpRecvCallbacks.insert(std::make_pair(socketId, std::move(recvCallback)));
    }
    recvCallbackLock.unlock();

    std::unique_lock closeCallbackLock(closeCallbacksMutex);
    if (closeCallback != nullptr) {
        closeCallbacks.insert(std::make_pair(socketId, std::make_pair(std::move(closeCallback), nullptr)));
    }
    closeCallbackLock.unlock();

    std::unique_lock keepAliveTimeoutsLock(keepAliveTimeoutsMutex);
    if (keepAliveTimeout > 0) {
        keepAliveTimeouts.insert(std::make_pair(socketId, std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() + (keepAliveTimeout * 1000)));
    }
    keepAliveTimeoutsLock.unlock();

    std::unique_lock socketsLock(socketsMutex);
    std::unique_lock sendBuffersLock(tcpSendBuffersMutex);

    tcpSendBuffers.insert(std::make_pair(socketId, std::vector<uint8_t>()));

    sockets.insert(std::make_pair(socketId, std::make_pair(SocketType::TCP_CONN, std::move(socket))));
    return socketId;
}

unsigned int SocketManager::addUDPSocket(std::shared_ptr<sock::UDPSocket> socket,
                          std::function<void(unsigned int, std::vector<uint8_t>, sock::IPv4Dir)> recvCallback,
                          std::function<void(unsigned int)> closeCallback) {
    unsigned int socketId = nextSocketId++;

    std::unique_lock recvCallbackLock(udpRecvCallbacksMutex);
    if (recvCallback != nullptr) {
        udpRecvCallbacks.insert(std::make_pair(socketId, std::move(recvCallback)));
    }
    recvCallbackLock.unlock();

    std::unique_lock closeCallbackLock(closeCallbacksMutex);
    if (closeCallback != nullptr) {
        closeCallbacks.insert(std::make_pair(socketId, std::make_pair(std::move(closeCallback), nullptr)));
    }
    closeCallbackLock.unlock();

    std::unique_lock socketsLock(socketsMutex);
    std::unique_lock sendBuffersLock(udpSendBuffersMutex);

    udpSendBuffers.insert(std::make_pair(socketId, std::vector<std::pair<sock::IPv4Dir, std::vector<uint8_t>>>()));

    sockets.insert(std::make_pair(socketId, std::make_pair(SocketType::UDP, std::move(socket))));
    return socketId;
}

void SocketManager::process() {
    std::unique_lock socketsLock(socketsMutex);
    if (sockets.empty()) return;

    std::vector<pollfd> fds;
    std::vector<unsigned int> socketIndexToId;

    for (auto& socket : sockets) {
        short events = POLLIN;
        if (socket.second.first == SocketType::TCP_CONN || socket.second.first == SocketType::UDP) {
            // If the type is TCP_CONN or UDP, it is guaranteed to be in the sendBuffers map
            if (socket.second.second->getLastResult() == sock::ResultType::NEEDS_WRITE) {
                events |= POLLOUT;
            }
        }

        socketIndexToId.push_back(socket.first);
        fds.push_back({socket.second.second->getSocket(), events, 0});
    }
    socketsLock.unlock();

#ifdef _WIN32
    int ret = WSAPoll(fds.data(), fds.size(), POLL_TIMEOUT);
#else
    int ret = poll(fds.data(), fds.size(), POLL_TIMEOUT);
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
            if (fd.revents & POLLHUP || fd.revents & POLLERR) {
                logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                            "Socket with ID " + std::to_string(socketIndexToId[i]) + " has been closed (socket error or disconnection)");
                close(socketIndexToId[i], true);
                i++;
                continue;
            }

            if (fd.revents & POLLOUT) {
                socketsLock.lock();
                if (sockets[socketIndexToId[i]].first == SocketType::TCP_CONN) {
                    sock::SocketStatus status = sockets[socketIndexToId[i]].second->getStatus();
                    if (status == sock::SocketStatus::CONNECTING) {
                        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketIndexToId[i]].second);
                        try {
                            socket->connect();
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Socket with ID " + std::to_string(socketIndexToId[i]) + " has connected");

                            std::unique_lock connectCallbacksLock(connectCallbacksMutex);
                            auto callback = connectCallbacks.find(socketIndexToId[i]);
                            if (callback != connectCallbacks.end()) callback->second(socketIndexToId[i]);
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
                        send(socketIndexToId[i]);
                    } else if (status == sock::SocketStatus::CLOSING) {
                        close(socketIndexToId[i]);
                    }
                } else if (sockets[socketIndexToId[i]].first == SocketType::UDP) {
                    sendto(socketIndexToId[i]);
                }
                socketsLock.unlock();
            }

            if (fd.revents & POLLIN) {
                socketsLock.lock();
                SocketType type = sockets[socketIndexToId[i]].first;
                if (type == SocketType::TCP) {
                    accept(socketIndexToId[i]);
                } else if (type == SocketType::TCP_CONN) {
                    sock::SocketStatus status = sockets[socketIndexToId[i]].second->getStatus();
                    if (status == sock::SocketStatus::CONNECTING) {
                        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketIndexToId[i]].second);
                        try {
                            socket->connect();
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Socket with ID " + std::to_string(socketIndexToId[i]) + " has connected");

                            std::unique_lock connectCallbacksLock(connectCallbacksMutex);
                            auto callback = connectCallbacks.find(socketIndexToId[i]);
                            if (callback != connectCallbacks.end()) callback->second(socketIndexToId[i]);
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
                        recv(socketIndexToId[i]);
                    } else if (status == sock::SocketStatus::CLOSING) {
                        close(socketIndexToId[i]);
                    }
                } else if (type == SocketType::UDP) {
                    recvfrom(socketIndexToId[i]);
                }
                socketsLock.unlock();
            }

            i++;
        }
    }

    // Check for keep alive timeouts
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    socketsLock.lock();
    std::unique_lock keepAliveTimeoutsLock(keepAliveTimeoutsMutex);
    for (auto& timeout : keepAliveTimeouts) {
        if (timeout.second < now) {
            if (sockets[timeout.first].first == SocketType::TCP_CONN) {
                logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                            "Socket with ID " + std::to_string(timeout.first) + " has exceeded its keep alive timeout, will be closed");
                close(timeout.first);
            }
        }
    }
    keepAliveTimeoutsLock.unlock();
    socketsLock.unlock();

    // Check for previously not closed sockets because of data in the send buffer and sockets that haven't been closed
    // because we were waiting for a response to the close request
    std::unique_lock closeQueueLock(closeQueueMutex);
    std::unique_lock tcpSendBuffersLock(tcpSendBuffersMutex);
    std::unique_lock udpSendBuffersLock(udpSendBuffersMutex);
    std::unique_lock closeTimeoutsLock(closeTimeoutsMutex);

    for (auto socketId = closeQueue.begin(); socketId != closeQueue.end(); ) {
        if (sockets.find(*socketId) == sockets.end()) {
            socketId = closeQueue.erase(socketId);
            continue;
        }

        auto socket = sockets[*socketId];
        if (socket.first == SocketType::TCP_CONN || socket.first == SocketType::UDP) {
            bool hasData = false;
            if (socket.first == SocketType::TCP_CONN) {
                hasData = !tcpSendBuffers[*socketId].empty();
            } else if (socket.first == SocketType::UDP) {
                hasData = !udpSendBuffers[*socketId].empty();
            }

            if (!hasData && closeTimeouts.find(*socketId) == closeTimeouts.end()) {
                if (close(*socketId)) {
                    socketId = closeQueue.erase(socketId);
                    continue;
                }
            } else if (closeTimeouts.find(*socketId) != closeTimeouts.end()) {
                if (closeTimeouts[*socketId] < now) {
                    if (close(*socketId, true)) {
                        socketId = closeQueue.erase(socketId);
                        continue;
                    }
                }
            }
        }

        socketId++;
    }
}

void SocketManager::connect(unsigned int socketId, sock::IPv4Dir address) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::TCP_CONN) return;

    auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketId].second);
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

bool SocketManager::send(unsigned int socketId, std::vector<uint8_t> data) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return false;
    if (sockets[socketId].first != SocketType::TCP_CONN) return false;

    size_t total = 0;
    while (total < data.size()) {
        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketId].second);
        size_t sent;
        try {
            sent = socket->send(data.data() + total, data.size() - total, 0);
        } catch (const sock::RetryableException& e) {
            std::unique_lock sendBuffersLock(tcpSendBuffersMutex);
            auto it = tcpSendBuffers.find(socketId);
            it->second.insert(it->second.end(), data.begin() + (unsigned int) total, data.end());
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

void SocketManager::send(unsigned int socketId) {
    std::unique_lock socketsLock(socketsMutex);
    std::unique_lock sendBuffersLock(tcpSendBuffersMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::TCP_CONN) return;

    auto it = tcpSendBuffers.find(socketId);
    if (it == tcpSendBuffers.end()) return;
    if (it->second.empty()) return;

    size_t oldSize = it->second.size();

    if (!send(socketId, it->second)) {
        it->second.erase(it->second.begin(), it->second.begin() + (long long) oldSize);
    }
}

bool SocketManager::sendto(unsigned int socketId, std::vector<uint8_t> data, sock::IPv4Dir address) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return false;
    if (sockets[socketId].first != SocketType::UDP) return false;

    auto socket = std::dynamic_pointer_cast<sock::UDPSocket>(sockets[socketId].second);

    struct sockaddr_in addr = util::ipv4ToSockAddr(address);

    try {
        socket->sendto(data.data(), data.size(), 0, (struct sockaddr*) &addr, sizeof(addr));
    } catch (const sock::RetryableException& e) {
        std::unique_lock sendBuffersLock(udpSendBuffersMutex);
        auto it = udpSendBuffers.find(socketId);
        it->second.emplace_back(address, std::move(data));
        return true;
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        close(socketId, true);
        return false;
    }

    return true;
}

void SocketManager::sendto(unsigned int socketId) {
    std::unique_lock socketsLock(socketsMutex);
    std::unique_lock sendBuffersLock(udpSendBuffersMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::UDP) return;

    auto it = udpSendBuffers.find(socketId);
    if (it == udpSendBuffers.end()) return;

    for (auto it_packets = it->second.begin(); it_packets != it->second.end(); it_packets++) {
        if (!sendto(socketId, it_packets->second, it_packets->first)) break;
        it_packets = it->second.erase(it_packets);
    }
}

void SocketManager::recv(unsigned int socketId) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::TCP_CONN) return;

    std::vector<uint8_t> recvBuf;
    recvBuf.resize(1024 * 16); // 16k should be enough for most (if not all) packets
    auto tcpSocket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketId].second);
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

        std::unique_lock recvCallbacksLock(tcpRecvCallbacksMutex);
        if (tcpRecvCallbacks.find(socketId) != tcpRecvCallbacks.end()) {
            tcpRecvCallbacks[socketId](socketId, std::move(recvBuf));
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

void SocketManager::recvfrom(unsigned int socketId) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::UDP) return;

    std::vector<uint8_t> recvBuf;
    recvBuf.resize(65535); // A UDP won't receive more than 65535 bytes (it's actually 65507, but we'll use 65535)

    struct sockaddr_in addr{};
    int addrLen = sizeof(addr);

    auto udpSocket = std::dynamic_pointer_cast<sock::UDPSocket>(sockets[socketId].second);
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
        sock::IPv4Dir dir{ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3], ntohs(addr.sin_port)};

        std::unique_lock recvCallbacksLock(udpRecvCallbacksMutex);
        if (udpRecvCallbacks.find(socketId) != udpRecvCallbacks.end()) {
            udpRecvCallbacks[socketId](socketId, std::move(recvBuf), dir);
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

void SocketManager::accept(unsigned int socketId) {
    std::unique_lock socketsLock(socketsMutex);
    //bool socketsLocked = true;

    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::TCP) return;

    auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketId].second);
    try {
        struct sockaddr addr{};
        int addrLen = sizeof(addr);
        auto newSocket = socket->accept(&addr, &addrLen);

        int keepAliveTimeout = 0;

        std::unique_lock keepAliveTimeoutsLock(keepAliveTimeoutsMutex);
        if (keepAliveTimeouts.find(socketId) != keepAliveTimeouts.end()) {
            keepAliveTimeout = (int) keepAliveTimeouts[socketId];
        }
        keepAliveTimeoutsLock.unlock();

        auto *addrIn = (struct sockaddr_in *) &addr;
        auto *ipv4addr = (uint8_t *) &addrIn->sin_addr.S_un.S_un_b;
        sock::IPv4Dir dir{ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3], addrIn->sin_port};

        std::unique_lock acceptCallbacksLock(acceptCallbacksMutex);
        std::unique_lock recvCallbacksLock(tcpRecvCallbacksMutex);
        std::unique_lock closeCallbacksLock(closeCallbacksMutex);

        auto acceptCallback = acceptCallbacks.find(socketId);
        auto recvCallback = tcpRecvCallbacks.find(socketId);
        auto closeCallback = closeCallbacks.find(socketId);

        std::function<void(unsigned int)> connectCallback;
        if (acceptCallback != acceptCallbacks.end()) {
            connectCallback = [acceptCallback, socketId, dir](unsigned int newSocketId) {
                acceptCallback->second(socketId, newSocketId, dir);
            };
        }

        unsigned int newSocketId = addTCPSocketConn(std::shared_ptr<sock::TCPSocket>(newSocket),
                                                    std::move(connectCallback),
                                                    (recvCallback == tcpRecvCallbacks.end()) ? nullptr
                                                                                             : recvCallback->second,
                                                    (closeCallback == closeCallbacks.end()) ? nullptr
                                                                                            : closeCallback->second.second,
                                                    keepAliveTimeout);

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

bool SocketManager::close(unsigned int socketId, bool force) {
    std::unique_lock socketsLock(socketsMutex);

    if (sockets.find(socketId) == sockets.end()) return false;

    std::unique_lock tcpSendBuffersLock(tcpSendBuffersMutex);
    std::unique_lock udpSendBuffersLock(udpSendBuffersMutex);
    std::unique_lock closeQueueLock(closeQueueMutex);

    bool hasData = false;
    if (sockets[socketId].first == SocketType::TCP_CONN) {
        hasData = !tcpSendBuffers[socketId].empty();
    } else if (sockets[socketId].first == SocketType::UDP) {
        hasData = !udpSendBuffers[socketId].empty();
    }

    if (!force && hasData && std::find(closeQueue.begin(), closeQueue.end(), socketId) == closeQueue.end()) {
        closeQueue.push_back(socketId);
    } else {
        try {
            sockets.find(socketId)->second.second->close(force);
        } catch (const sock::RetryableException& e) {
            std::unique_lock closeTimeoutsLock(closeTimeoutsMutex);
            if (closeTimeouts.find(socketId) == closeTimeouts.end()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                closeTimeouts.insert(std::make_pair(socketId, now + CLOSE_TIMEOUT));
            }
            return false;
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        }

        std::unique_lock closeCallbacksLock(closeCallbacksMutex);
        auto closeCallback = closeCallbacks.find(socketId);

        if (closeCallback != closeCallbacks.end() && closeCallback->second.first != nullptr) {
            closeCallback->second.first(socketId);
        }

        removeSocket(socketId);
        return true;
    }

    return false;
}

void SocketManager::removeSocket(unsigned int socketId) {
    std::scoped_lock lock(socketsMutex, tcpSendBuffersMutex, udpSendBuffersMutex, acceptCallbacksMutex,
                          connectCallbacksMutex, tcpRecvCallbacksMutex, udpRecvCallbacksMutex,
                          closeCallbacksMutex,keepAliveTimeoutsMutex, closeTimeoutsMutex);

    sockets.erase(socketId);
    acceptCallbacks.erase(socketId);
    connectCallbacks.erase(socketId);
    tcpRecvCallbacks.erase(socketId);
    udpRecvCallbacks.erase(socketId);
    closeCallbacks.erase(socketId);
    tcpSendBuffers.erase(socketId);
    udpSendBuffers.erase(socketId);
    keepAliveTimeouts.erase(socketId);
    closeTimeouts.erase(socketId);

//    auto it = std::find(closeQueue.begin(), closeQueue.end(), socketId);
//    if (it != closeQueue.end()) closeQueue.erase(it);
}

bool SocketManager::isClosed(unsigned int socketId) {
    std::unique_lock socketsLock(socketsMutex);
    return sockets.find(socketId) == sockets.end();
}

void SocketManager::cleanup() {
    std::scoped_lock lock(socketsMutex, tcpSendBuffersMutex, udpSendBuffersMutex, acceptCallbacksMutex,
                          connectCallbacksMutex, tcpRecvCallbacksMutex, udpRecvCallbacksMutex,
                          closeCallbacksMutex,keepAliveTimeoutsMutex, closeTimeoutsMutex);

    for (auto& socket : sockets) {
        close(socket.first, true);
    }
}