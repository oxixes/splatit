#include "socketManager.hpp"

#include <chrono>
#include <optional>
#include "../util/util.hpp"

// TODO Add mutexes to allow multithreaded access to the socket manager
// TODO Make a connect function

SocketManager::SocketManager(std::shared_ptr<Logger::Logger> logger) : logger(std::move(logger)) {}

unsigned int SocketManager::addTCPSocket(std::shared_ptr<sock::TCPSocket> socket,
                                         std::function<void(unsigned int, unsigned int, sock::IPv4Dir)> acceptCallback,
                                         std::function<void(unsigned int)> closeCallback,
                                         std::function<void(unsigned int, std::vector<unsigned char>)> connRecvCallback,
                                         std::function<void(unsigned int)> connCloseCallback, int keepAliveTimeout) {
    unsigned int socketId = nextSocketId++;

    if (acceptCallback != nullptr) {
        acceptCallbacks.insert(std::make_pair(socketId, std::move(acceptCallback)));
    }

    if (closeCallback != nullptr && connCloseCallback != nullptr) {
        closeCallbacks.insert(std::make_pair(socketId, std::make_pair(std::move(closeCallback),
                                                                      std::move(connCloseCallback))));
    }

    if (connRecvCallback != nullptr) {
        recvCallbacks.insert(std::make_pair(socketId, std::move(connRecvCallback)));
    }

    if (keepAliveTimeout > 0) {
        keepAliveTimeouts.insert(std::make_pair(socketId, keepAliveTimeout));
    }

    sockets.insert(std::make_pair(socketId, std::make_pair(SocketType::TCP, std::move(socket))));
    return socketId;
}

unsigned int SocketManager::addTCPSocketConn(std::shared_ptr<sock::TCPSocket> socket,
                                             std::function<void(unsigned int)> connectCallback,
                                             std::function<void(unsigned int, std::vector<unsigned char>)> recvCallback,
                                             std::function<void(unsigned int)> closeCallback, int keepAliveTimeout) {
    unsigned int socketId = nextSocketId++;

    if (connectCallback != nullptr) {
        connectCallbacks.insert(std::make_pair(socketId, std::move(connectCallback)));
    }

    if (recvCallback != nullptr) {
        recvCallbacks.insert(std::make_pair(socketId, std::move(recvCallback)));
    }

    if (closeCallback != nullptr) {
        closeCallbacks.insert(std::make_pair(socketId, std::make_pair(std::move(closeCallback), nullptr)));
    }

    if (keepAliveTimeout > 0) {
        keepAliveTimeouts.insert(std::make_pair(socketId, std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() + (keepAliveTimeout * 1000)));
    }

    sendBuffers.insert(std::make_pair(socketId, std::vector<unsigned char>()));

    sockets.insert(std::make_pair(socketId, std::make_pair(SocketType::TCP_CONN, std::move(socket))));
    return socketId;
}

void SocketManager::process() {
    if (sockets.empty()) return;

    std::vector<pollfd> fds;
    std::vector<unsigned int> socketIndexToId;

    for (auto& socket : sockets) {
        short events = POLLIN;
        if (socket.second.first == SocketType::TCP_CONN) {
            // If the type is TCP_CONN, it is guaranteed to be in the sendBuffers map
            if (socket.second.second->getLastResult() == sock::ResultType::NEEDS_WRITE) {
                events |= POLLOUT;
            }
        }

        socketIndexToId.push_back(socket.first);
        fds.push_back({socket.second.second->getSocket(), POLLIN, 0});
    }

#ifdef _WIN32
    int ret = WSAPoll(fds.data(), fds.size(), POLL_TIMEOUT);
#else
    int ret = poll(fds.data(), fds.size(), 0);
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
                if (sockets[socketIndexToId[i]].first == SocketType::TCP_CONN) {
                    sock::SocketStatus status = sockets[socketIndexToId[i]].second->getStatus();
                    if (status == sock::SocketStatus::CONNECTING) {
                        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketIndexToId[i]].second);
                        try {
                            socket->connect();
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Socket with ID " + std::to_string(socketIndexToId[i]) + " has connected");
                            auto callback = connectCallbacks.find(socketIndexToId[i]);
                            if (callback != connectCallbacks.end()) {
                                callback->second(socketIndexToId[i]);
                            }
                        } catch (const sock::RetryableException &e) {
                            i++;
                            continue;
                        } catch (const sock::FatalException &e) {
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Error connecting from socket with ID " + std::to_string(socketIndexToId[i]) +
                                        ": " + e.what());
                            close(socketIndexToId[i], true);
                            i++;
                            continue;
                        }
                    } else if (status == sock::SocketStatus::CONNECTED) {
                        send(socketIndexToId[i]);
                    } else if (status == sock::SocketStatus::CLOSING) {
                        close(socketIndexToId[i]);
                    }
                }
            }

            if (fd.revents & POLLIN) {
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
                            auto callback = connectCallbacks.find(socketIndexToId[i]);
                            if (callback != connectCallbacks.end()) {
                                callback->second(socketIndexToId[i]);
                            }
                        } catch (const sock::RetryableException &e) {
                            i++;
                            continue;
                        } catch (const sock::FatalException &e) {
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Error connecting from socket with ID " + std::to_string(socketIndexToId[i]) +
                                        ": " + e.what());
                            close(socketIndexToId[i], true);
                            i++;
                            continue;
                        }
                    } else if (status == sock::SocketStatus::CONNECTED) {
                        recv(socketIndexToId[i]);
                    } else if (status == sock::SocketStatus::CLOSING) {
                        close(socketIndexToId[i]);
                    }
                }
            }

            i++;
        }
    }

    // Check for keep alive timeouts
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& timeout : keepAliveTimeouts) {
        if (timeout.second < now) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                        "Socket with ID " + std::to_string(timeout.first) + " has exceeded its keep alive timeout, will be closed");
            close(timeout.first);
        }
    }

    // Check for previously not closed sockets because of data in the send buffer and sockets that haven't been closed
    // because we were waiting for a response to the close request
    for (auto& socketId : closeQueue) {
        auto socket = sockets[socketId];
        if (socket.first == SocketType::TCP_CONN) {
            if (sendBuffers[socketId].empty() && closeTimeouts.find(socketId) == closeTimeouts.end()) {
                close(socketId);
            } else if (closeTimeouts.find(socketId) != closeTimeouts.end()) {
                if (closeTimeouts[socketId] < now) {
                    close(socketId, true);
                }
            }
        }
    }
}

bool SocketManager::send(unsigned int socketId, std::vector<unsigned char> data) {
    if (sockets.find(socketId) == sockets.end()) return false;
    if (sockets[socketId].first != SocketType::TCP_CONN) return false;

    size_t total = 0;
    while (total < data.size()) {
        auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketId].second);
        size_t sent;
        try {
            sent = socket->send(data.data() + total, data.size() - total, 0);
        } catch (const sock::RetryableException& e) {
            auto it = sendBuffers.find(socketId);
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
    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::TCP_CONN) return;

    auto it = sendBuffers.find(socketId);
    if (it == sendBuffers.end()) return;
    if (it->second.empty()) return;

    size_t oldSize = it->second.size();

    if (!send(socketId, it->second)) it->second.erase(it->second.begin(), it->second.begin() + (long long) oldSize);
}

void SocketManager::recv(unsigned int socketId) {
    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::TCP_CONN) return;

    // FIXME: Maybe change this to a std::array?
    std::vector<unsigned char> recvBuf;
    recvBuf.resize(1024 * 16); // 16k should be enough for most (if not all) packets
    auto tcpSocket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketId].second);
    try {
        int recvBytes = tcpSocket->recv(recvBuf.data(), recvBuf.capacity(), 0);
        if (recvBytes == 0) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                        "Socket with ID " + std::to_string(socketId) + " has been closed (read 0 bytes)");
            // FIXME: Handle SSL 0 read differently to guarantee that a close notification is sent?
            close(socketId, true);
            return;
        } else if (std::find(closeQueue.begin(), closeQueue.end(), socketId) != closeQueue.end()) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                        "Socket with ID " + std::to_string(socketId) + " has received some data"
                                                                                 " but is in the close queue, so it will be ignored.");
            return;
        }

        recvBuf.resize(recvBytes);
        if (recvCallbacks.find(socketId) != recvCallbacks.end()) {
            recvCallbacks[socketId](socketId, std::move(recvBuf));
        }
    } catch (const sock::RetryableException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Socket with ID " + std::to_string(socketId) + " threw a retryable exception,"
                                                                             " this should NOT happen, but will be ignored.");
        return;
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Socket with ID " + std::to_string(socketId) + " has been closed (read threw a fatal exception)");
        close(socketId, true);
        return;
    }
}

void SocketManager::accept(unsigned int socketId) {
    if (sockets.find(socketId) == sockets.end()) return;
    if (sockets[socketId].first != SocketType::TCP) return;

    auto socket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketId].second);
    try {
        struct sockaddr addr{};
        int addrLen = sizeof(addr);
        auto newSocket = socket->accept(&addr, &addrLen);

        int keepAliveTimeout = 0;
        if (keepAliveTimeouts.find(socketId) != keepAliveTimeouts.end()) {
            keepAliveTimeout = (int) keepAliveTimeouts[socketId];
        }

        auto *addrIn = (struct sockaddr_in *) &addr;
        auto *ipv4addr = (uint8_t *) &addrIn->sin_addr.S_un.S_un_b;
        sock::IPv4Dir dir{ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3], addrIn->sin_port};

        auto acceptCallback = acceptCallbacks.find(socketId);
        auto recvCallback = recvCallbacks.find(socketId);
        auto closeCallback = closeCallbacks.find(socketId);

        std::function<void(unsigned int)> connectCallback;
        if (acceptCallback != acceptCallbacks.end()) {
            connectCallback = [acceptCallback, socketId, dir](unsigned int newSocketId) {
                acceptCallback->second(socketId, newSocketId, dir);
            };
        }

        unsigned int newSocketId = addTCPSocketConn(std::shared_ptr<sock::TCPSocket>(newSocket),
                                                    std::move(connectCallback),
                                                    (recvCallback == recvCallbacks.end()) ? nullptr
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

void SocketManager::close(unsigned int socketId, bool force) {
    if (sockets.find(socketId) == sockets.end()) return;

    if (!force && sendBuffers.find(socketId) != sendBuffers.end() && !sendBuffers[socketId].empty() &&
        std::find(closeQueue.begin(), closeQueue.end(), socketId) == closeQueue.end()) {
        closeQueue.push_back(socketId);
    } else {
        try {
            sockets.find(socketId)->second.second->close(force);
        } catch (const sock::RetryableException& e) {
            if (closeTimeouts.find(socketId) == closeTimeouts.end()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                closeTimeouts.insert(std::make_pair(socketId, now + CLOSE_TIMEOUT));
            }
            return;
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        }

        auto closeCallback = closeCallbacks.find(socketId);
        if (closeCallback != closeCallbacks.end() && closeCallback->second.first != nullptr)
            closeCallbacks.find(socketId)->second.first(socketId);

        removeSocket(socketId);
    }
}

void SocketManager::removeSocket(unsigned int socketId) {
    sockets.erase(socketId);
    acceptCallbacks.erase(socketId);
    connectCallbacks.erase(socketId);
    recvCallbacks.erase(socketId);
    closeCallbacks.erase(socketId);
    sendBuffers.erase(socketId);
    keepAliveTimeouts.erase(socketId);
    closeTimeouts.erase(socketId);

    auto it = std::find(closeQueue.begin(), closeQueue.end(), socketId);
    if (it != closeQueue.end()) closeQueue.erase(it);
}