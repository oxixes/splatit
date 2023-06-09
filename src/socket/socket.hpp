#ifndef SPLATOON_SERVER_SOCKET_HPP
#define SPLATOON_SERVER_SOCKET_HPP

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#endif

#include <stdexcept>

namespace sock {

bool initialize();
void cleanup();

class RetryableException : public std::runtime_error {
public:
    explicit RetryableException(const std::string& what_arg) : std::runtime_error(what_arg) {};
    explicit RetryableException(const char* what_arg) : std::runtime_error(what_arg) {};
};

class FatalException : public std::runtime_error {
public:
    explicit FatalException(const std::string& what_arg) : std::runtime_error(what_arg) {};
    explicit FatalException(const char* what_arg) : std::runtime_error(what_arg) {};
};

class Socket {
public:
    Socket(int domain, int type, int protocol);
    virtual ~Socket();

    void setsockopt(int level, int optname, const void* optval, socklen_t optlen) const;
    void bind(const struct sockaddr* addr, socklen_t addrlen) const;
    void listen(int backlog = SOMAXCONN) const;

    virtual Socket* accept(struct sockaddr* addr, socklen_t* addrlen) const;
    virtual void connect(const struct sockaddr* addr, socklen_t addrlen);
    virtual int send(const void* buf, size_t len, int flags) const;
    virtual int recv(void* buf, size_t len, int flags) const;

    virtual void close();
    virtual void shutdown() const;

#ifdef _WIN32
    [[nodiscard]] SOCKET getSocket() const;
#else
    [[nodiscard]] int getSocket() const;
#endif

protected:
#ifdef _WIN32
    explicit Socket(SOCKET socket);
    SOCKET acceptAux(struct sockaddr* addr, socklen_t* addrlen) const;

    SOCKET socket;
#else
    explicit TCPSocket(int socket);
    int SOCKET acceptAux(struct sockaddr* addr, socklen_t* addrlen) const;

    int socket;
#endif

#ifdef _WIN32
        static std::string getError(int error);
#endif
};

class TCPSocket : public Socket {
public:
#ifdef _WIN32
    TCPSocket() : Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) {};
#else
    TCPSocket() : Socket(AF_INET, SOCK_STREAM, 0) {};
#endif
    ~TCPSocket() override = default;

protected:
#ifdef _WIN32
    explicit TCPSocket(SOCKET socket) : Socket(socket) {};
#else
    explicit TCPSocket(int socket) : Socket(socket) {};
#endif
};

class UDPSocket : public Socket {
public:
#ifdef _WIN32
    UDPSocket() : Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) {};
#else
    UDPSocket() : Socket(AF_INET, SOCK_DGRAM, 0) {};
#endif
    ~UDPSocket() override = default;

protected:
#ifdef _WIN32
    explicit UDPSocket(SOCKET socket) : Socket(socket) {};
#else
    explicit UDPSocket(int socket) : Socket(socket) {};
#endif
};

} // namespace sock

#endif //SPLATOON_SERVER_SOCKET_HPP
