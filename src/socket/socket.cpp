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

void Socket::setsockopt(int level, int optname, const void* optval, socklen_t optlen) const {
    int result;
#ifdef _WIN32
    result = ::setsockopt(socket, level, optname, static_cast<const char*>(optval), optlen);
#else
    result = ::setsockopt(socket, level, optname, optval, optlen);
#endif

    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to set socket option: " + util::getWSAError(WSAGetLastError()));
#else
        throw FatalException("Failed to set socket option: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::bind(const struct sockaddr* addr, socklen_t addrlen) const {
    int result = ::bind(socket, addr, addrlen);
    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to bind socket: " + util::getWSAError(WSAGetLastError()));
#else
        throw FatalException("Failed to bind socket: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::listen(int backlog) const {
    int result = ::listen(socket, backlog);
    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to listen on socket: " + util::getWSAError(WSAGetLastError()));
#else
        throw FatalException("Failed to listen on socket: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::setBlocking(bool blocking) const {
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
        throw FatalException("Failed to set socket blocking: " + util::getWSAError(WSAGetLastError()));
#else
        throw FatalException("Failed to set socket blocking: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::close() {
    int result;
#ifdef _WIN32
    result = closesocket(socket);
#else
    result = ::close(socket);
#endif

    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to close socket: " + util::getWSAError(WSAGetLastError()));
#else
        throw FatalException("Failed to close socket: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::shutdown() const {
    int result;
#ifdef _WIN32
    result = ::shutdown(socket, SD_BOTH);
#else
    result = ::shutdown(socket, SHUT_RDWR);
#endif

    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to shutdown socket: " + util::getWSAError(WSAGetLastError()));
#else
        throw FatalException("Failed to shutdown socket: " + std::string(strerror(errno)));
#endif
    }
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

#ifdef _WIN32
SOCKET TCPSocket::acceptAux(struct sockaddr* addr, socklen_t* addrlen) const {
#else
int Socket::acceptAux(struct sockaddr* addr, socklen_t* addrlen) const {
#endif
#ifdef _WIN32
    SOCKET newSocket = ::accept(socket, addr, addrlen);

    if (newSocket == INVALID_SOCKET) {
        throw FatalException("Failed to accept socket: " + util::getWSAError(WSAGetLastError()));
    }
#else
    int newSocket = ::accept(socket, addr, addrlen);

    if (newSocket < 0) {
        throw FatalException("Failed to accept socket: " + std::string(strerror(errno)));
    }
#endif

    return newSocket;
}

TCPSocket* TCPSocket::accept(struct sockaddr* addr, socklen_t* addrlen) const {
    return new TCPSocket(acceptAux(addr, addrlen));
}

void TCPSocket::connect(const struct sockaddr* addr, socklen_t addrlen) {
    int result = ::connect(socket, addr, addrlen);
    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to connect socket: " + util::getWSAError(WSAGetLastError()));
#else
        throw FatalException("Failed to connect socket: " + std::string(strerror(errno)));
#endif
    }
}

int TCPSocket::send(const void* buf, size_t len, int flags) const {
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
            throw RetryableException("Failed to send data: " + util::getWSAError(error));
        } else {
            throw FatalException("Failed to send data: " + util::getWSAError(error));
        }
#else
        int error = errno;
        if (error == EAGAIN || error == EWOULDBLOCK) {
            throw RetryableException("Failed to send data: " + std::string(strerror(error)));
        } else {
            throw FatalException("Failed to send data: " + std::string(strerror(error)));
        }
#endif
    }

    return result;
}

int TCPSocket::recv(void* buf, size_t len, int flags) const {
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
            throw RetryableException("Failed to receive data: " + util::getWSAError(error));
        } else {
            throw FatalException("Failed to receive data: " + util::getWSAError(error));
        }
#else
        int error = errno;
        if (error == EAGAIN || error == EWOULDBLOCK) {
            throw RetryableException("Failed to receive data: " + std::string(strerror(error)));
        } else {
            throw FatalException("Failed to receive data: " + std::string(strerror(error)));
        }
#endif
    }

    return result;
}

void TCPSocket::sendall(const void* buf, size_t len, int flags) const {
    size_t total = 0;
    int numTries = 0;
    while (total < len) {
        try {
            int result = send(static_cast<const char *>(buf) + total, len - total, flags);
            total += result;
        } catch (const RetryableException& e) {
            if (numTries >= MAX_TRIES) {
                throw FatalException("Failed to send data: " + std::string(e.what()));
            }

            numTries++;
            continue;
        }
    }
}

} // namespace sock