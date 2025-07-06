#ifndef SPLATOON_SERVER_HTTP_SERVER_HPP
#define SPLATOON_SERVER_HTTP_SERVER_HPP

#include <openssl/ssl.h>
#include <queue>
#include <condition_variable>
#include <regex>

#include "../logger.hpp"
#include "../socket/socketManager.hpp"
#include "../http/parser/request.hpp"
#include "../socket/sslSocket.hpp"
#include "parser/response.hpp"
#include "../db/database.hpp"

#define MAX_PAYLOAD_SIZE 0x6400000 // 100 MiB

namespace http {

struct Context {
    std::shared_ptr<Logger::Logger> logger;
    sock::IPv4Addr client;
    uint32_t clientSockId;
    std::shared_ptr<Request> request;
    int status;
    std::shared_ptr<std::queue<std::shared_ptr<Promise>>> promisesQueue;
    std::shared_ptr<std::mutex> queueMutex;
    std::shared_ptr<std::condition_variable> queueCV;
};

class Server {
public:
    Server(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<SocketManager> socketMgr,
           sock::IPv4Addr listenDir, int keepAliveTimeout, bool ssl, EVP_PKEY* key = nullptr, X509* cert = nullptr);
    ~Server();

    void listen(int workerCount, const std::function<void()>& closeFunc);
    void stop();

    void registerRoute(const std::string& host, const std::string& path, std::function<void(
            Server*, std::shared_ptr<Context>)> func);

    void registerRegexRoute(const std::string& host, const std::string& path, std::function<void(
            Server*, std::shared_ptr<Context>)> func);

    void unregisterHost(const std::string& host);

    void registerErrorPage(const std::string& host, std::function<void(
            Server*, std::shared_ptr<Context>)> func);

    void sendResponse(std::shared_ptr<http::Context> context, std::unique_ptr<http::Response> response, bool keepAlive = false);

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<SocketManager> socketMgr;

    int keepAliveTimeout;
    uint32_t mainSocketID;
    std::shared_ptr<sock::TCPSocket> mainSocket;

    std::unordered_map<uint32_t, std::vector<uint8_t>> buffers;
    std::vector<std::thread> threads;

    std::queue<std::pair<uint32_t, std::shared_ptr<http::Request>>> requestsQueue;
    std::shared_ptr<std::queue<std::shared_ptr<Promise>>> promisesQueue = std::make_shared<std::queue<std::shared_ptr<Promise>>>();
    std::shared_ptr<std::mutex> queueMutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::condition_variable> queueCV = std::make_shared<std::condition_variable>();

    // This is a map of maps, the first key is the host, the second key is the path for that given host
    std::unordered_map<std::string, std::unordered_map<std::string, std::function<void(
            Server*, std::shared_ptr<Context>)>>> routes;

    std::unordered_map<std::string, std::vector<std::pair<std::regex, std::function<void(
            Server*, std::shared_ptr<Context>)>>>> regexRoutes;

    std::unordered_map<std::string, std::function<void(
            Server*, std::shared_ptr<Context>)>> errorPages;
    std::mutex routesMutex;

    std::map<uint32_t, sock::IPv4Addr> clients;
    std::mutex clientsMutex;

    bool shouldStop = false;

    void serverThread();
    void onAccept(uint32_t newSockId, sock::IPv4Addr dir);
    void onClose(uint32_t sockId);
    void onDataReceived(uint32_t sockId, std::vector<uint8_t> data);

    static std::unique_ptr<http::Response> getError(http::Version version, int status);
    void sendError(uint32_t sockId, int status, const std::shared_ptr<Request>& request, sock::IPv4Addr client);
    // Sent when a request is not available, as it couldn't be parsed
    void sendError(uint32_t sockId, int status);
};

} // namespace http

#endif // SPLATOON_SERVER_HTTP_SERVER_HPP