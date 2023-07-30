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
#include <cstdint>

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

class SSLException : public std::runtime_error {
public:
    explicit SSLException(const std::string& what_arg) : std::runtime_error(what_arg) {};
    explicit SSLException(const char* what_arg) : std::runtime_error(what_arg) {};
};

struct IPv4Dir {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint16_t port;
};

enum class SocketStatus {
    NOT_CONNECTED,
    CONNECTING,
    CONNECTED,
    CLOSING,
    CLOSED,
    LISTENING,
    FAILURE
};

enum class ResultType {
    SUCCESS,
    NEEDS_READ,
    NEEDS_WRITE
};

class Socket {
public:
    Socket(int domain, int type, int protocol);
    virtual ~Socket();

    void setsockopt(int level, int optname, const void* optval, socklen_t optlen);
    void bind(const struct sockaddr* addr, socklen_t addrlen);
    void listen(int backlog = SOMAXCONN);
    void setBlocking(bool blocking);

    virtual void close(bool force);

#ifdef _WIN32
    [[nodiscard]] SOCKET getSocket() const;
#else
    [[nodiscard]] int getSocket() const;
#endif

    [[nodiscard]] SocketStatus getStatus() const;
    [[nodiscard]] ResultType getLastResult() const;

protected:
#ifdef _WIN32
    explicit Socket(SOCKET socket);

    SOCKET socket;
#else
    explicit TCPSocket(int socket);
    int SOCKET acceptAux(struct sockaddr* addr, socklen_t* addrlen) const;

    int socket;
#endif

    SocketStatus status = SocketStatus::NOT_CONNECTED;
    ResultType lastResult = ResultType::SUCCESS;
};

class TCPSocket : public Socket {
public:
#ifdef _WIN32
    TCPSocket() : Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) {};
#else
    TCPSocket() : Socket(AF_INET, SOCK_STREAM, 0) {};
#endif
    ~TCPSocket() override = default;

    virtual TCPSocket* accept(struct sockaddr* addr, socklen_t* addrlen);
    virtual void connect(const struct sockaddr* addr, socklen_t addrlen);
    virtual void connect();
    virtual int send(const void* buf, size_t len, int flags);
    virtual int recv(void* buf, size_t len, int flags);

protected:
#ifdef _WIN32
    explicit TCPSocket(SOCKET socket) : Socket(socket) {};

    SOCKET acceptAux(struct sockaddr* addr, socklen_t* addrlen);
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

    void sendto(const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);
    int recvfrom(void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen);

protected:
#ifdef _WIN32
    explicit UDPSocket(SOCKET socket) : Socket(socket) {};
#else
    explicit UDPSocket(int socket) : Socket(socket) {};
#endif
};

} // namespace sock

#endif //SPLATOON_SERVER_SOCKET_HPP
