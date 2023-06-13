#ifndef SPLATOON_SERVER_SOCKETMANAGER_HPP
#define SPLATOON_SERVER_SOCKETMANAGER_HPP

#include <memory>
#include <vector>
#include <map>

#include "socket.hpp"
#include "../logger.hpp"

// Defines the amount of time to wait for a socket to be ready for any operation.
// It is not infinity because we need to check if enough time has passed to close
// keep alive sockets.
#define POLL_TIMEOUT 200

enum class SocketType {
    TCP,
    TCP_CONN
};

class SocketManager {
public:
    explicit SocketManager(std::shared_ptr<Logger::Logger> logger);
    ~SocketManager() = default;

    unsigned int addTCPSocket(std::shared_ptr<sock::TCPSocket> socket,
                     void(*acceptCallback)(unsigned int, unsigned int, sock::IPv4Dir),
                     void(*closeCallback)(unsigned int),
                     void(*connRecvCallback)(unsigned int, std::vector<unsigned char>),
                     void(*connCloseCallback)(unsigned int), int keepAliveTimeout = 0);
    unsigned int addTCPSocketConn(std::shared_ptr<sock::TCPSocket> socket,
                         void(*recvCallback)(unsigned int, std::vector<unsigned char>),
                         void(*closeCallback)(unsigned int), int keepAliveTimeout = 0);

    void process();

    bool send(unsigned int socketId, std::vector<unsigned char> data);
    void close(unsigned int socketId, bool force = false);

private:
    std::map<unsigned int, std::pair<SocketType, std::shared_ptr<sock::Socket>>> sockets;
    std::map<unsigned int, void(*)(unsigned int, unsigned int, sock::IPv4Dir)> acceptCallbacks;
    std::map<unsigned int, void(*)(unsigned int, std::vector<unsigned char>)> recvCallbacks;
    // The first callback in the pair is the socket close callback, and the second is the close callback for
    // any connections accepted by the socket.
    std::map<unsigned int, std::pair<void(*)(unsigned int), void(*)(unsigned int)>> closeCallbacks;
    std::map<unsigned int, std::vector<unsigned char>> sendBuffers;
    std::map<unsigned int, unsigned long long> keepAliveTimeouts;
    std::vector<unsigned int> closeQueue;

    unsigned int nextSocketId = 0;

    std::shared_ptr<Logger::Logger> logger;

    void removeSocket(unsigned int socketId);

    // Sends data from the write buffer of the socket with the given ID.
    void send(unsigned int socketId);
    void accept(unsigned int socketId);
};


#endif //SPLATOON_SERVER_SOCKETMANAGER_HPP
