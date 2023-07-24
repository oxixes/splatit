#include "socket.hpp"

#include "../util/util.hpp"

namespace sock {

bool initialize() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

Socket::Socket(int domain, int type, int protocol) {
#ifdef _WIN32
    socket = ::socket(domain, type, protocol);

    if (socket == INVALID_SOCKET) {
        throw FatalException("Failed to create socket: " + util::getWSAError(WSAGetLastError()));
    }
#else
    socket = ::socket(domain, type, protocol);

    if (socket < 0) {
        throw FatalException("Failed to create socket: " + std::string(strerror(errno)));
    }
#endif
}

Socket::~Socket() {
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

#ifdef _WIN32
    Socket::Socket(SOCKET socket) : socket(socket) {}
#else
    Socket::Socket(int socket) : socket(socket) {}
#endif

void Socket::setsockopt(int level, int optname, const void* optval, socklen_t optlen) {
    int result;
#ifdef _WIN32
    result = ::setsockopt(socket, level, optname, static_cast<const char*>(optval), optlen);
#else
    result = ::setsockopt(socket, level, optname, optval, optlen);
#endif

    if (result < 0) {
#ifdef _WIN32
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to set socket option: " + util::getWSAError(WSAGetLastError()));
#else
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to set socket option: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::bind(const struct sockaddr* addr, socklen_t addrlen) {
    int result = ::bind(socket, addr, addrlen);
    if (result < 0) {
#ifdef _WIN32
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to bind socket: " + util::getWSAError(WSAGetLastError()));
#else
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to bind socket: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::listen(int backlog) {
    int result = ::listen(socket, backlog);
    if (result < 0) {
#ifdef _WIN32
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to listen on socket: " + util::getWSAError(WSAGetLastError()));
#else
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to listen on socket: " + std::string(strerror(errno)));
#endif
    }

    status = SocketStatus::LISTENING;
}

void Socket::setBlocking(bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    int result = ioctlsocket(socket, FIONBIO, &mode);
#else
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        throw FatalException("Failed to get socket flags: " + std::string(strerror(errno)));
    }

    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    int result = fcntl(socket, F_SETFL, flags);
#endif

    if (result < 0) {
#ifdef _WIN32
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to set socket blocking: " + util::getWSAError(WSAGetLastError()));
#else
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to set socket blocking: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::close(bool force) {
    // Try to shut down the socket gracefully, but don't care if it fails
    // as we're closing it anyway
    ::shutdown(socket, SD_BOTH);

    int result;
#ifdef _WIN32
    result = closesocket(socket);
#else
    result = ::close(socket);
#endif

    if (result < 0) {
#ifdef _WIN32
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to close socket: " + util::getWSAError(WSAGetLastError()));
#else
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to close socket: " + std::string(strerror(errno)));
#endif
    }

    status = SocketStatus::CLOSED;
}

#ifdef _WIN32
SOCKET Socket::getSocket() const {
    return socket;
}
#else
int Socket::getSocket() const {
    return socket;
}
#endif

SocketStatus Socket::getStatus() const {
    return status;
}

ResultType Socket::getLastResult() const {
    return lastResult;
}

#ifdef _WIN32
SOCKET TCPSocket::acceptAux(struct sockaddr* addr, socklen_t* addrlen) {
#else
int Socket::acceptAux(struct sockaddr* addr, socklen_t* addrlen) const {
#endif
#ifdef _WIN32
    SOCKET newSocket = ::accept(socket, addr, addrlen);

    if (newSocket == INVALID_SOCKET) {
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to accept socket: " + util::getWSAError(WSAGetLastError()));
    }
#else
    int newSocket = ::accept(socket, addr, addrlen);

    if (newSocket < 0) {
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to accept socket: " + std::string(strerror(errno)));
    }
#endif

    return newSocket;
}

TCPSocket* TCPSocket::accept(struct sockaddr* addr, socklen_t* addrlen) {
    if (status != SocketStatus::LISTENING) {
        throw FatalException("Not listening, cannot continue (tried accepting)");
    }

    auto newSocket = new TCPSocket(acceptAux(addr, addrlen));
    newSocket->status = SocketStatus::CONNECTED;
    return newSocket;
}

void TCPSocket::connect(const struct sockaddr* addr, socklen_t addrlen) {
    if (status != SocketStatus::NOT_CONNECTED) {
        throw FatalException("Not disconnected, cannot continue (tried connecting)");
    }

    int result = ::connect(socket, addr, addrlen);
#ifdef _WIN32
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) {
            status = SocketStatus::CONNECTING;
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to connect socket: " + util::getWSAError(error));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to connect socket: " + util::getWSAError(WSAGetLastError()));
        }
    }
#else
    if (result < 0) {
        int error = errno;
        if (error == EAGAIN || error == EINPROGRESS) {
            status = SocketStatus::CONNECTING;
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to connect socket: " + std::string(strerror(error)));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to connect socket: " + std::string(strerror(errno)));
        }
    }
#endif

    status = SocketStatus::CONNECTED;
}

void TCPSocket::connect() {
    if (status != SocketStatus::CONNECTING) {
        throw FatalException("Not connecting, cannot continue (tried connecting)");
    }

    // This is supposed to be called after select() has indicated that the socket is ready for writing
    status = SocketStatus::CONNECTED;
    lastResult = ResultType::SUCCESS;
}

int TCPSocket::send(const void* buf, size_t len, int flags) {
    if (status != SocketStatus::CONNECTED) {
        throw FatalException("Not connected, cannot continue (tried sending)");
    }

    int result;
#ifdef _WIN32
    result = ::send(socket, static_cast<const char*>(buf), static_cast<int>(len), flags);
#else
    result = ::send(socket, buf, len, flags);
#endif

    if (result < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to send data: " + util::getWSAError(error));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to send data: " + util::getWSAError(error));
        }
#else
        int error = errno;
        if (error == EAGAIN || error == EWOULDBLOCK) {
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to send data: " + std::string(strerror(error)));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to send data: " + std::string(strerror(error)));
        }
#endif
    }

    lastResult = ResultType::SUCCESS;

    return result;
}

int TCPSocket::recv(void* buf, size_t len, int flags) {
    if (status != SocketStatus::CONNECTED) {
        throw FatalException("Not connected, cannot continue (tried reading)");
    }

    int result;
#ifdef _WIN32
    result = ::recv(socket, static_cast<char*>(buf), static_cast<int>(len), flags);
#else
    result = ::recv(socket, buf, len, flags);
#endif

    if (result < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            lastResult = ResultType::NEEDS_READ;
            throw RetryableException("Failed to receive data: " + util::getWSAError(error));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to receive data: " + util::getWSAError(error));
        }
#else
        int error = errno;
        if (error == EAGAIN || error == EWOULDBLOCK) {
            lastResult = ResultType::NEEDS_READ;
            throw RetryableException("Failed to receive data: " + std::string(strerror(error)));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to receive data: " + std::string(strerror(error)));
        }
#endif
    }

    lastResult = ResultType::SUCCESS;

    return result;
}

void UDPSocket::sendto(const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen) {
#ifdef _WIN32
    int result = ::sendto(socket, (const char*) buf, (int) len, flags, dest_addr, addrlen);
#else
    int result = ::sendto(socket, buf, len, flags, dest_addr, addrlen);
#endif

    if (result < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAECONNRESET || error == WSAECONNREFUSED) return; // This is sent by Windows to indicate the last packet was dropped
                                                                        // so we can just ignore it
        if (error == WSAEWOULDBLOCK) {
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to send data: " + util::getWSAError(error));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to send data: " + util::getWSAError(error));
        }
#else
        int error = errno;
        if (error == ECONNREFUSED || error == ECONNRESET) return; // This is sent by Linux to indicate the last packet was dropped
                                                                  // so we can just ignore it
        if (error == EAGAIN || error == EWOULDBLOCK) {
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to send data: " + std::string(strerror(error)));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to send data: " + std::string(strerror(error)));
        }
#endif
    }
}

int UDPSocket::recvfrom(void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
#ifdef _WIN32
    int result = ::recvfrom(socket, (char*) buf, (int) len, flags, src_addr, addrlen);
#else
    int result = ::recvfrom(socket, buf, len, flags, src_addr, addrlen);
#endif

    if (result < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAECONNRESET || error == WSAECONNREFUSED) return 0; // This is sent by Windows to indicate the last packet was dropped
                                                                          // so we can just ignore it
        if (error == WSAEWOULDBLOCK) {
            lastResult = ResultType::NEEDS_READ;
            throw RetryableException("Failed to receive data: " + util::getWSAError(error));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to receive data: " + util::getWSAError(error));
        }
#else
        int error = errno;
        if (error == ECONNREFUSED || error == ECONNRESET) return 0; // This is sent by Linux to indicate the last packet was dropped
                                                                    // so we can just ignore it
        if (error == EAGAIN || error == EWOULDBLOCK) {
            lastResult = ResultType::NEEDS_READ;
            throw RetryableException("Failed to receive data: " + std::string(strerror(error)));
        } else {
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to receive data: " + std::string(strerror(error)));
        }
#endif
    }

    return result;
}

} // namespace sock