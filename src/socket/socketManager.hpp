#ifndef SPLATOON_SERVER_SOCKETMANAGER_HPP
#define SPLATOON_SERVER_SOCKETMANAGER_HPP

#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <functional>

#include "socket.hpp"
#include "../logger.hpp"

// Defines the amount of time to wait for a socket to be ready for any operation.
// It is not infinity because we need to check if enough time has passed to close
// keep alive sockets.
#define POLL_TIMEOUT 200
// Defines the amount of time to wait after a close notification has been sent to
// a socket before force closing it if a response hasn't been received.
#define CLOSE_TIMEOUT 3000

enum class SocketType {
    TCP,
    TCP_CONN,
    UDP
};

struct SocketInfo {
    std::shared_ptr<sock::Socket> socket;
    SocketType type;
    std::function<void(uint32_t, uint32_t, sock::IPv4Addr)> acceptCallback;
    std::function<void(uint32_t)> connectCallback;
    std::function<void(uint32_t, std::vector<uint8_t>)> tcpRecvCallback;
    std::function<void(uint32_t, std::vector<uint8_t>, sock::IPv4Addr)> udpRecvCallback;
    // The first callback in the pair is the socket close callback, and the second is the close callback for
    // any connections accepted by the socket (only if type is TCP, and not TCP_CONN).
    std::pair<std::function<void(uint32_t)>, std::function<void(uint32_t)>> closeCallback;

    int64_t keepAliveTimeout;
    int64_t closeTimeout;

    std::vector<uint8_t> tcpSendBuffer;
    std::vector<std::pair<sock::IPv4Addr, std::vector<uint8_t>>> udpSendBuffer;
};

class SocketManager {
public:
    explicit SocketManager(std::shared_ptr<Logger::Logger> logger);
    ~SocketManager() = default;

    uint32_t addTCPSocket(std::shared_ptr<sock::TCPSocket> socket,
                     std::function<void(uint32_t, uint32_t, sock::IPv4Addr)> acceptCallback,
                     std::function<void(uint32_t)> closeCallback,
                     std::function<void(uint32_t, std::vector<uint8_t>)> connRecvCallback,
                     std::function<void(uint32_t)> connCloseCallback, int keepAliveTimeout = 0);
    uint32_t addTCPSocketConn(std::shared_ptr<sock::TCPSocket> socket,
                     std::function<void(uint32_t)> connectCallback,
                     std::function<void(uint32_t, std::vector<uint8_t>)> recvCallback,
                     std::function<void(uint32_t)> closeCallback, int keepAliveTimeout = 0);
    uint32_t addUDPSocket(std::shared_ptr<sock::UDPSocket> socket,
                     std::function<void(uint32_t, std::vector<uint8_t>, sock::IPv4Addr)> recvCallback,
                     std::function<void(uint32_t)> closeCallback);

    void process(uint64_t ms);

    void connect(uint32_t socketId, sock::IPv4Addr address);
    bool send(uint32_t socketId, std::vector<uint8_t> data);
    bool sendto(uint32_t socketId, std::vector<uint8_t> data, sock::IPv4Addr address);
    bool close(uint32_t socketId, bool force = false);

    bool isClosed(uint32_t socketId);

    void cleanup();

private:
    std::unordered_map<uint32_t, SocketInfo> sockets;
    std::recursive_mutex socketsMutex;
    std::vector<uint32_t> closeQueue;
    std::recursive_mutex closeQueueMutex;

    uint32_t nextSocketId = 0;

    std::shared_ptr<Logger::Logger> logger;

    // Sends data from the write buffer of the socket with the given ID.
    void send(uint32_t socketId);
    void sendto(uint32_t socketId);
    void recv(uint32_t socketId);
    void recvfrom(uint32_t socketId);
    void accept(uint32_t socketId);
};


#endif //SPLATOON_SERVER_SOCKETMANAGER_HPP
