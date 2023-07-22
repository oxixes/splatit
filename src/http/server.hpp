#ifndef SPLATOON_SERVER_SERVER_HPP
#define SPLATOON_SERVER_SERVER_HPP

#include <openssl/ssl.h>
#include <queue>
#include <condition_variable>

#include "../logger.hpp"
#include "../socket/socketManager.hpp"
#include "../http/parser/request.hpp"
#include "../socket/sslSocket.hpp"
#include "parser/response.hpp"
#include "../db/database.hpp"

#define MAX_PAYLOAD_SIZE 0x6400000 // 100 MiB

class HTTP_Server {
public:
    HTTP_Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SocketManager> socketMgr,
                sock::IPv4Dir listenDir, int keepAliveTimeout, EVP_PKEY* key = nullptr, X509* cert = nullptr);
    ~HTTP_Server();

    void listen(int workerCount, const std::function<void()>& closeFunc);
    void stop();

    unsigned int registerCloseCall(std::function<void()> closeFunc);
    void unregisterCloseCall(unsigned int id);

    void registerRoute(const std::string& host, const std::string& path, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Dir, bool&, bool&,
            std::function<unsigned int(std::function<void()>)>, std::function<void(unsigned int)>)> func);

    void registerErrorPage(const std::string& host, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Dir, int)> func);

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<SocketManager> socketMgr;

    int keepAliveTimeout;
    unsigned int mainSocketID;
    std::shared_ptr<sock::SSLSocket> mainSocket;

    std::unordered_map<unsigned int, std::vector<unsigned char>> buffers;
    std::vector<std::thread> threads;

    std::queue<std::pair<unsigned int, http::Request>> requestsQueue;
    std::mutex requestsQueueMutex;

    std::mutex workerMutex;
    std::condition_variable workerCV;

    // This is a map of maps, the first key is the host, the second key is the path for that given host
    std::unordered_map<std::string, std::unordered_map<std::string, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Dir, bool&, bool&,
            std::function<unsigned int(std::function<void()>)>, std::function<void(unsigned int)>)>>> routes;
    std::mutex routesMutex;

    std::unordered_map<std::string, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Dir, int)>> errorPages;
    std::mutex errorPagesMutex;

    std::map<unsigned int, sock::IPv4Dir> clients;
    std::mutex clientsMutex;

    unsigned int closeCallID = 0;
    std::map<unsigned int, std::function<void()>> closeCalls;
    std::mutex closeCallsMutex;

    bool shouldStop = false;

    void serverThread();
    void onAccept(unsigned int newSockId, sock::IPv4Dir dir);
    void onClose(unsigned int sockId);
    void onDataReceived(unsigned int sockId, std::vector<unsigned char> data);

    static http::Response getError(http::Version version, int status);
    void sendError(unsigned int sockId, int status, const http::Request& request, sock::IPv4Dir client);
    // Sent when a request is not available, as it couldn't be parsed
    void sendError(unsigned int sockId, int status);
};

#endif // SPLATOON_SERVER_SERVER_HPP