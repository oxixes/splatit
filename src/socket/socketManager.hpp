#ifndef SPLATOON_SERVER_SOCKETMANAGER_HPP
#define SPLATOON_SERVER_SOCKETMANAGER_HPP

#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <thread>

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
    TCP_CONN
};

class SocketManager {
public:
    explicit SocketManager(std::shared_ptr<Logger::Logger> logger);
    ~SocketManager() = default;

    unsigned int addTCPSocket(std::shared_ptr<sock::TCPSocket> socket,
                     std::function<void(unsigned int, unsigned int, sock::IPv4Dir)> acceptCallback,
                     std::function<void(unsigned int)> closeCallback,
                     std::function<void(unsigned int, std::vector<unsigned char>)> connRecvCallback,
                     std::function<void(unsigned int)> connCloseCallback, int keepAliveTimeout = 0);
    unsigned int addTCPSocketConn(std::shared_ptr<sock::TCPSocket> socket,
                         std::function<void(unsigned int)> connectCallback,
                         std::function<void(unsigned int, std::vector<unsigned char>)> recvCallback,
                         std::function<void(unsigned int)> closeCallback, int keepAliveTimeout = 0);

    void process();

    void connect(unsigned int socketId, sock::IPv4Dir address);
    bool send(unsigned int socketId, std::vector<unsigned char> data);
    bool close(unsigned int socketId, bool force = false);

private:
    std::map<unsigned int, std::pair<SocketType, std::shared_ptr<sock::Socket>>> sockets;
    std::recursive_mutex socketsMutex;
    std::map<unsigned int, std::function<void(unsigned int, unsigned int, sock::IPv4Dir)>> acceptCallbacks;
    std::recursive_mutex acceptCallbacksMutex;
    std::map<unsigned int, std::function<void(unsigned int)>> connectCallbacks;
    std::recursive_mutex connectCallbacksMutex;
    std::map<unsigned int, std::function<void(unsigned int, std::vector<unsigned char>)>> recvCallbacks;
    std::recursive_mutex recvCallbacksMutex;
    // The first callback in the pair is the socket close callback, and the second is the close callback for
    // any connections accepted by the socket.
    std::map<unsigned int, std::pair<std::function<void(unsigned int)>, std::function<void(unsigned int)>>> closeCallbacks;
    std::recursive_mutex closeCallbacksMutex;
    std::map<unsigned int, std::vector<unsigned char>> sendBuffers;
    std::recursive_mutex sendBuffersMutex;
    std::map<unsigned int, unsigned long long> keepAliveTimeouts;
    std::recursive_mutex keepAliveTimeoutsMutex;
    std::map<unsigned int, unsigned long long> closeTimeouts;
    std::recursive_mutex closeTimeoutsMutex;
    std::vector<unsigned int> closeQueue;
    std::recursive_mutex closeQueueMutex;

    unsigned int nextSocketId = 0;

    std::shared_ptr<Logger::Logger> logger;

    void removeSocket(unsigned int socketId);

    // Sends data from the write buffer of the socket with the given ID.
    void send(unsigned int socketId);
    void recv(unsigned int socketId);
    void accept(unsigned int socketId);
};


#endif //SPLATOON_SERVER_SOCKETMANAGER_HPP
