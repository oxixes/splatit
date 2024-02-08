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
#include <fcntl.h>
#endif

#include <stdexcept>
#include <cstdint>
#include <functional>

namespace sock {

bool initialize();
void cleanup();

struct IPv4Addr {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint16_t port;

    bool operator ==(const IPv4Addr& other) const {
        return a == other.a && b == other.b && c == other.c && d == other.d && port == other.port;
    }
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
    explicit Socket(int socket);

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

    int acceptAux(struct sockaddr* addr, socklen_t* addrlen);
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

// Define hash function for IPv4Addr
namespace std {
    template<>
    struct hash<sock::IPv4Addr> {
        size_t operator()(const sock::IPv4Addr& addr) const {
            size_t ha = hash<uint8_t>()(addr.a);
            size_t hb = hash<uint8_t>()(addr.b);
            size_t hc = hash<uint8_t>()(addr.c);
            size_t hd = hash<uint8_t>()(addr.d);
            size_t hport = hash<uint16_t>()(addr.port);

            // Combine hashes (This is the way Boost combines hashes)
            return ha ^ (hb + 0x9e3779b9 + (ha << 6) + (ha >> 2))
                   ^ (hc + 0x9e3779b9 + (hb << 6) + (hb >> 2))
                   ^ (hd + 0x9e3779b9 + (hc << 6) + (hc >> 2))
                   ^ (hport + 0x9e3779b9 + (hd << 6) + (hd >> 2));
        }
    };
} // namespace std

#endif //SPLATOON_SERVER_SOCKET_HPP