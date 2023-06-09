#include "socket.hpp"

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
        throw FatalException("Failed to create socket: " + getError(WSAGetLastError()));
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
        throw FatalException("Failed to set socket option: " + getError(WSAGetLastError()));
#else
        throw FatalException("Failed to set socket option: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::bind(const struct sockaddr* addr, socklen_t addrlen) const {
    int result = ::bind(socket, addr, addrlen);
    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to bind socket: " + getError(WSAGetLastError()));
#else
        throw FatalException("Failed to bind socket: " + std::string(strerror(errno)));
#endif
    }
}

void Socket::listen(int backlog) const {
    int result = ::listen(socket, backlog);
    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to listen on socket: " + getError(WSAGetLastError()));
#else
        throw FatalException("Failed to listen on socket: " + std::string(strerror(errno)));
#endif
    }
}

Socket* Socket::accept(struct sockaddr* addr, socklen_t* addrlen) const {
    return new Socket(acceptAux(addr, addrlen));
}

void Socket::connect(const struct sockaddr* addr, socklen_t addrlen) {
    int result = ::connect(socket, addr, addrlen);
    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to connect socket: " + getError(WSAGetLastError()));
#else
        throw FatalException("Failed to connect socket: " + std::string(strerror(errno)));
#endif
    }
}

int Socket::send(const void* buf, size_t len, int flags) const {
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
            throw RetryableException("Failed to send data: " + getError(error));
        } else {
            throw FatalException("Failed to send data: " + getError(error));
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

int Socket::recv(void* buf, size_t len, int flags) const {
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
            throw RetryableException("Failed to receive data: " + getError(error));
        } else {
            throw FatalException("Failed to receive data: " + getError(error));
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

void Socket::close() {
    int result;
#ifdef _WIN32
    result = closesocket(socket);
#else
    result = ::close(socket);
#endif

    if (result < 0) {
#ifdef _WIN32
        throw FatalException("Failed to close socket: " + getError(WSAGetLastError()));
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
        throw FatalException("Failed to shutdown socket: " + getError(WSAGetLastError()));
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
std::string Socket::getError(int error) {
    char* buffer;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<char*>(&buffer), 0, nullptr);
    std::string message(buffer);
    LocalFree(buffer);
    return message;
}
#endif

#ifdef _WIN32
SOCKET Socket::acceptAux(struct sockaddr* addr, socklen_t* addrlen) const {
#else
int Socket::acceptAux(struct sockaddr* addr, socklen_t* addrlen) const {
#endif
#ifdef _WIN32
    SOCKET newSocket = ::accept(socket, addr, addrlen);

    if (newSocket == INVALID_SOCKET) {
        throw FatalException("Failed to accept socket: " + getError(WSAGetLastError()));
    }
#else
    int newSocket = ::accept(socket, addr, addrlen);

    if (newSocket < 0) {
        throw FatalException("Failed to accept socket: " + std::string(strerror(errno)));
    }
#endif

    return newSocket;
}

} // namespace sock