#ifndef SPLATOON_SERVER_HTTP_SERVER_HPP
#define SPLATOON_SERVER_HTTP_SERVER_HPP

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

namespace http {

class Server {
public:
    Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SocketManager> socketMgr,
           sock::IPv4Addr listenDir, int keepAliveTimeout, EVP_PKEY* key = nullptr, X509* cert = nullptr);
    ~Server();

    void listen(int workerCount, const std::function<void()>& closeFunc);
    void stop();

    uint32_t registerCloseCall(std::function<void()> closeFunc);
    void unregisterCloseCall(uint32_t id);

    void registerRoute(const std::string& host, const std::string& path, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Addr, bool&, bool&,
            std::function<uint32_t(std::function<void()>)>, std::function<void(uint32_t)>)> func);

    void registerErrorPage(const std::string& host, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Addr, int)> func);

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<SocketManager> socketMgr;

    int keepAliveTimeout;
    uint32_t mainSocketID;
    std::shared_ptr<sock::SSLSocket> mainSocket;

    std::unordered_map<uint32_t, std::vector<uint8_t>> buffers;
    std::vector<std::thread> threads;

    std::queue<std::pair<uint32_t, http::Request>> requestsQueue;
    std::mutex requestsQueueMutex;

    std::mutex workerMutex;
    std::condition_variable workerCV;

    // This is a map of maps, the first key is the host, the second key is the path for that given host
    std::unordered_map<std::string, std::unordered_map<std::string, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Addr, bool&, bool&,
            std::function<uint32_t(std::function<void()>)>, std::function<void(uint32_t)>)>>> routes;
    std::mutex routesMutex;

    std::unordered_map<std::string, std::function<http::Response(
            std::shared_ptr<Logger::Logger>, http::Request, sock::IPv4Addr, int)>> errorPages;
    std::mutex errorPagesMutex;

    std::map<uint32_t, sock::IPv4Addr> clients;
    std::mutex clientsMutex;

    uint32_t closeCallID = 0;
    std::map<uint32_t, std::function<void()>> closeCalls;
    std::mutex closeCallsMutex;

    bool shouldStop = false;

    void serverThread();
    void onAccept(uint32_t newSockId, sock::IPv4Addr dir);
    void onClose(uint32_t sockId);
    void onDataReceived(uint32_t sockId, std::vector<uint8_t> data);

    static http::Response getError(http::Version version, int status);
    void sendError(uint32_t sockId, int status, const http::Request& request, sock::IPv4Addr client);
    // Sent when a request is not available, as it couldn't be parsed
    void sendError(uint32_t sockId, int status);
};

} // namespace http

#endif // SPLATOON_SERVER_HTTP_SERVER_HPP