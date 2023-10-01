#ifndef SPLATOON_SERVER_RMC_SERVER_HPP
#define SPLATOON_SERVER_RMC_SERVER_HPP

#include <functional>
#include <memory>
#include <queue>
#include <condition_variable>

#include "../prudp/server.hpp"
#include "types.hpp"
#include "request.hpp"
#include "response.hpp"
#include "../../util/util.hpp"

namespace nex::rmc {

struct ClientInfo {
    prudp::PRUDPAddress address;
    uint8_t minorVersion;
    uint8_t substreamId;
    uint32_t pid = 0;
};

struct CallInfo {
    std::function<void(ClientInfo, Request, const std::vector<T_ptr>&)> callback;
    std::function<std::vector<T_ptr>(uint8_t minorVersion, std::vector<uint8_t> data)> parser;
};

struct RequestInfo {
    ClientInfo client;
    Request request;
    std::vector<T_ptr> params;
};

// These are traits and functions used to get the types of the parameters of the callback function
// without having to specify them manually.
template<typename T>
struct function_traits;

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...)>
{
    enum { arity = sizeof...(Args) };

    template <size_t i>
    struct arg
    {
        typedef typename std::tuple_element<i, std::tuple<Args...>>::type type;
    };
};

class Server {
public:
    virtual ~Server() = default;

    void registerPRUDPServer(const std::shared_ptr<prudp::Server>& server, uint8_t listenPort, int workerCount);

protected:
    explicit Server(std::shared_ptr<Logger::Logger> logger);

    template<typename T, typename F>
    void registerCall(T* self, F callback, uint8_t protoId, uint32_t methodId, uint16_t extProtoId = 0) {
        constexpr auto size = function_traits<F>::arity;

        registerCall(self, callback, std::make_index_sequence<size - 2>{}, protoId, methodId, extProtoId);
    }

    // Requests are always sent without expecting an answer, and in this case will be notifications
    template<typename Msg> requires (std::is_base_of_v<Msg, Request> || std::is_base_of_v<Msg, Response>)
    void sendMsg(ClientInfo client, const Msg& msg, const std::vector<T_ptr>& params) {
        if (shouldStop) return;
        std::vector<uint8_t> data;

        for (auto& param : params) {
            auto paramData = param->encode();
            // Log the data
//            std::cout << "Param: ";
//            for (auto& byte : paramData) {
//                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int) byte;
//            }
//            std::cout << std::endl;

            data.insert(data.end(), paramData.begin(), paramData.end());
        }

        auto requestData = msg.encode(data.size());
        requestData.insert(requestData.end(), data.begin(), data.end());

        logMsg(msg, client.address, false);
        sendData(client.address, std::move(requestData), client.substreamId);
    }

    virtual void onConnect(prudp::PRUDPAddress address, uint32_t pid);
    virtual void onDisconnect(prudp::PRUDPAddress address);

    static Response createError(const Request& req, Error error);

    std::shared_ptr<Logger::Logger> logger;
    Logger::group logGroup = Logger::group::SETUP; // This should be set by the constructor of the derived class

    std::function<uint32_t(std::function<void()>)> registerCloseCall;
    std::function<void(uint32_t)> unregisterCloseCall;

    bool shouldStop = false;

    std::unordered_map<prudp::PRUDPAddress, uint32_t> pidMap;
private:
    // These are functions used to call the callback function with the correct parameters.
    // They expand the parameter vector into the parameters of the callback function.
    template<typename T, typename Func, typename... Types, std::size_t... I> requires (std::is_base_of_v<Type, Types> && ...)
    auto call_callback(T* self, Func func, ClientInfo client, Request req, const std::vector<T_ptr>& arr, std::index_sequence<I...>) {
        return (self->*func)(client, req, std::move(*std::dynamic_pointer_cast<Types>(arr.at(I)))...);
    }

    template<typename T, typename F, std::size_t ... I>
    void registerCall(T* self, F callback, std::index_sequence<I ...> sequence, uint8_t protoId, uint32_t methodId, uint16_t extProtoId = 0) {
        auto func = [this, self, callback, sequence](ClientInfo client, Request req,
                const std::vector<T_ptr>& params) {
            call_callback<T, F,
                    typename function_traits<
                            typename std::decay<F>::type>::template arg<I + 2>::type...
            >(self, callback, client, req, params, sequence);
        };

        auto parser = [](uint8_t minorVersion, std::vector<uint8_t> data) {
            return ParamParser<
                    typename function_traits<
                            typename std::decay<F>::type>::template arg<I + 2>::type...
            >::decode(minorVersion, std::move(data));
        };

        calls.emplace(std::make_tuple(protoId, extProtoId, methodId), CallInfo{func, parser});
    }

    void serverThread();

    void start(int workerCount);
    void stop();

    void onData(prudp::PRUDPAddress addr, uint8_t minor_version, uint8_t substreamId, std::vector<uint8_t> data);

    template <typename Msg> requires (std::is_base_of_v<Msg, Request> || std::is_base_of_v<Msg, Response>)
    void logMsg(const Msg& msg, prudp::PRUDPAddress addr, bool incoming) {
        std::string type = (std::is_base_of_v<Msg, Request>) ? "REQ" : "RES";
        std::string protoId = "Proto: " + std::to_string(msg.protocolId);
        protoId.resize(10, ' ');

        std::string exProtoId = "ExtProto: " + std::to_string(msg.extendedProtocolId);
        exProtoId.resize(15, ' ');

        std::string methodId = "Method: " + std::to_string(msg.methodId);
        methodId.resize(11, ' ');

        std::string callId = "Call: " + std::to_string(msg.callId);

        std::string extra;
        if (std::is_base_of_v<Msg, Response>) {
            auto& res = (const Response&) msg;
            if (res.success) {
                extra = " | Success";
            } else {
                std::stringstream ss;
                ss << " | Error: " << std::setw(sizeof(uint32_t) * 2)
                     << std::setfill('0') << std::hex << static_cast<uint32_t>(res.error);
                extra = ss.str();
            }
        }

        std::string output = "[ RMC ] [" + util::ipv4ToString(addr.address) + ":" + std::to_string(addr.address.port) + "] ";
        output += (incoming) ? "<- " : "-> ";
        output += type + " | " + protoId + " | " + exProtoId + " | " + methodId + " | " + callId + extra;

        logger->log(Logger::level::DEBUG, logGroup, output);
    }

    std::function<void(prudp::PRUDPAddress, std::vector<uint8_t>, uint8_t)> sendData;

    // The key is a tuple of the protocol id, extended protocol id and method id.
    std::map<std::tuple<uint8_t, uint16_t, uint32_t>, CallInfo> calls;

    std::queue<RequestInfo> requestsQueue;
    std::mutex queueMutex;

    std::mutex workerMutex;
    std::condition_variable workerCV;

    std::vector<std::thread> threads;

    uint32_t closeCallID = 0;
    std::map<uint32_t, std::function<void()>> closeCalls;
    std::mutex closeCallsMutex;
};


} // namespace nex::rmc

#endif //SPLATOON_SERVER_RMC_SERVER_HPP
