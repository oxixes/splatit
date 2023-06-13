#include "socketManager.hpp"

#include <chrono>
#include "../util/util.hpp"

// TODO Add mutexes to allow multithreaded access to the socket manager

SocketManager::SocketManager(std::shared_ptr<Logger::Logger> logger) : logger(std::move(logger)) {}

unsigned int SocketManager::addTCPSocket(std::shared_ptr<sock::TCPSocket> socket,
                                         void (*acceptCallback)(unsigned int, unsigned int, sock::IPv4Dir),
                                         void (*closeCallback)(unsigned int),
                                         void (*connRecvCallback)(unsigned int, std::vector<unsigned char>),
                                         void (*connCloseCallback)(unsigned int), int keepAliveTimeout) {
    unsigned int socketId = nextSocketId++;

    if (acceptCallback != nullptr) {
        acceptCallbacks.insert(std::make_pair(socketId, acceptCallback));
    }

    if (closeCallback != nullptr && connCloseCallback != nullptr) {
        closeCallbacks.insert(std::make_pair(socketId, std::make_pair(closeCallback, connCloseCallback)));
    }

    if (connRecvCallback != nullptr) {
        recvCallbacks.insert(std::make_pair(socketId, connRecvCallback));
    }

    if (keepAliveTimeout > 0) {
        keepAliveTimeouts.insert(std::make_pair(socketId, keepAliveTimeout));
    }

    sockets.insert(std::make_pair(socketId, std::make_pair(SocketType::TCP, std::move(socket))));
    return socketId;
}

unsigned int SocketManager::addTCPSocketConn(std::shared_ptr<sock::TCPSocket> socket,
                                             void (*recvCallback)(unsigned int, std::vector<unsigned char>),
                                             void (*closeCallback)(unsigned int), int keepAliveTimeout) {
    unsigned int socketId = nextSocketId++;

    if (recvCallback != nullptr) {
        recvCallbacks.insert(std::make_pair(socketId, recvCallback));
    }

    if (closeCallback != nullptr) {
        closeCallbacks.insert(std::make_pair(socketId, std::make_pair(closeCallback, nullptr)));
    }

    if (keepAliveTimeout > 0) {
        keepAliveTimeouts.insert(std::make_pair(socketId, std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() + keepAliveTimeout));
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
            if (!sendBuffers[socket.first].empty()) {
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
                            "Socket with ID " + std::to_string(socketIndexToId[i]) + " has been closed");
                close(socketIndexToId[i], true);
                continue;
            }

            if (fd.revents & POLLOUT) send(socketIndexToId[i]);

            if (fd.revents & POLLIN) {
                SocketType type = sockets[socketIndexToId[i]].first;
                if (type == SocketType::TCP) {
                    accept(socketIndexToId[i]);
                } else if (type == SocketType::TCP_CONN) {
                    std::vector<unsigned char> recvBuf;
                    recvBuf.reserve(1024 * 16); // 16k should be enough for most (if not all) packets
                    auto tcpSocket = std::dynamic_pointer_cast<sock::TCPSocket>(sockets[socketIndexToId[i]].second);
                    try {
                        int recvBytes = tcpSocket->recv(recvBuf.data(), recvBuf.capacity(), 0);
                        if (recvBytes == 0) {
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Socket with ID " + std::to_string(socketIndexToId[i]) + " has been closed");
                            close(socketIndexToId[i], true);
                            continue;
                        } else if (std::find(closeQueue.begin(), closeQueue.end(), socketIndexToId[i]) != closeQueue.end()) {
                            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                        "Socket with ID " + std::to_string(socketIndexToId[i]) + " has received some data"
                                                                                                 " but is in the close queue, so it will be ignored.");
                            continue;
                        }

                        if (recvCallbacks.find(socketIndexToId[i]) != recvCallbacks.end()) {
                            recvCallbacks[socketIndexToId[i]](socketIndexToId[i], std::move(recvBuf));
                        }
                    } catch (const sock::RetryableException& e) {
                        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                    "Socket with ID " + std::to_string(socketIndexToId[i]) + " threw a retryable exception,"
                                                                                             " this should NOT happen, but will be ignored.");
                        continue;
                    } catch (const sock::FatalException& e) {
                        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                                    "Socket with ID " + std::to_string(socketIndexToId[i]) + " has been closed");
                        close(socketIndexToId[i], true);
                        continue;
                    }

                }
            }

            i++;
        }
    }

    // Check for keep alive timeouts
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& timeout : keepAliveTimeouts) {
        if (timeout.second < now) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                        "Socket with ID " + std::to_string(timeout.first) + " has exceeded its keep alive timeout, will be closed");
            close(timeout.first);
        }
    }

    // Check for previously not closed sockets because of data in the send buffer
    for (auto& socketId : closeQueue) {
        auto socket = sockets[socketId];
        if (socket.first == SocketType::TCP_CONN) {
            if (sendBuffers[socketId].empty()) {
                close(socketId, true);
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

        auto recvCallback = recvCallbacks.find(socketId);
        auto closeCallback = closeCallbacks.find(socketId);

        unsigned int newSocketId = addTCPSocketConn(std::shared_ptr<sock::TCPSocket>(newSocket),
                (recvCallback == recvCallbacks.end()) ? nullptr : recvCallback->second,
                (closeCallback == closeCallbacks.end()) ? nullptr : closeCallback->second.second, keepAliveTimeout);

        auto* addrIn = (struct sockaddr_in*) &addr;
        auto* ipv4addr = (uint8_t*) &addrIn->sin_addr.S_un.S_un_b;
        sock::IPv4Dir dir {ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3], addrIn->sin_port};

        logger->log(Logger::level::DEBUG, Logger::group::NETWORK,
                    "Socket with ID " + std::to_string(socketId) + " has accepted a new connection with ID " +
                    std::to_string(newSocketId));

        auto acceptCallback = acceptCallbacks.find(socketId);
        if (acceptCallback != acceptCallbacks.end()) acceptCallback->second(socketId, newSocketId, dir);
    } catch (const sock::FatalException& e) {
        logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        // We cannot close the socket as it might be caused because of a failed SSL negotiation and not because of a
        // real error
        // TODO Differentiate between SSL errors and real errors
        //close(socketId, true);
    }
}

void SocketManager::close(unsigned int socketId, bool force) {
    if (sockets.find(socketId) == sockets.end()) return;

    if (!force && sendBuffers.find(socketId) != sendBuffers.end() && !sendBuffers[socketId].empty() &&
        std::find(closeQueue.begin(), closeQueue.end(), socketId) == closeQueue.end()) {
        closeQueue.push_back(socketId);
    } else {
        auto closeCallback = closeCallbacks.find(socketId);
        if (closeCallback != closeCallbacks.end() && closeCallback->second.first != nullptr)
            closeCallbacks.find(socketId)->second.first(socketId);
        try {
            sockets.find(socketId)->second.second->close();
        } catch (const sock::FatalException& e) {
            logger->log(Logger::level::DEBUG, Logger::group::NETWORK, std::string(e.what()));
        }
        removeSocket(socketId);
    }
}

void SocketManager::removeSocket(unsigned int socketId) {
    sockets.erase(socketId);
    acceptCallbacks.erase(socketId);
    recvCallbacks.erase(socketId);
    closeCallbacks.erase(socketId);
    sendBuffers.erase(socketId);
    keepAliveTimeouts.erase(socketId);

    auto it = std::find(closeQueue.begin(), closeQueue.end(), socketId);
    if (it != closeQueue.end()) closeQueue.erase(it);
}