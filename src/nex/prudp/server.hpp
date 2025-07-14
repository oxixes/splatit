#ifndef SPLATOON_SERVER_PRUDP_SERVER_HPP
#define SPLATOON_SERVER_PRUDP_SERVER_HPP

#include <memory>
#include <set>
#include <unordered_map>
#include <map>
#include <span>

#include "../../socket/socket.hpp"
#include "../../logger.hpp"
#include "../../socket/socketManager.hpp"
#include "../../settingsManager.hpp"
#include "packet.hpp"

// Defines the amount of time between the last message received and the next ping.
#define PING_INTERVAL 20000

// Defines the maximum amount of packets in the packet queue.
#define MAX_PACKET_QUEUE_SIZE 100

// Defines the maximum amount of times a packet can be sent before the connection is closed.
#define MAX_RETRIES 3

// Defines the time a packet is delayed before being sent again.
#define RETRY_INTERVAL 1000

// Defines the maximum size of a packet (in bytes).
#define MAX_PACKET_SIZE 962

namespace nex::prudp {

    struct PRUDPAddress {
        sock::IPv4Addr address;
        uint8_t vPort;
        uint8_t streamType; // This will be used to differentiate clients (ignored otherwise),
                            // although in practice it will always be 0xA. I don't know the differences
                            // between other stream types, as they've not been documented.
        uint8_t srcVPort;
        uint8_t srcStreamType;

        bool operator ==(const PRUDPAddress& other) const {
            return address == other.address && vPort == other.vPort && streamType == other.streamType &&
                    srcVPort == other.srcVPort && srcStreamType == other.srcStreamType;
        }
    };

} // namespace nex::prudp

// Create a hash function for PRUDPAddress, so we can use it as a key in an unordered_map.
namespace std {
    template<>
    struct hash<nex::prudp::PRUDPAddress> {
        std::size_t operator()(const nex::prudp::PRUDPAddress& addr) const {
            size_t h1 = hash<sock::IPv4Addr>()(addr.address);
            size_t h2 = hash<uint8_t>()(addr.vPort);
            size_t h3 = hash<uint8_t>()(addr.streamType);
            size_t h4 = hash<uint8_t>()(addr.srcVPort);
            size_t h5 = hash<uint8_t>()(addr.srcStreamType);

            // Combine hashes (This is the way Boost combines hashes)
            size_t seed = 0;
            seed ^= h1 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h4 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h5 + 0x9e3779b9 + (seed << 6) + (seed >> 2);

            return seed;
        }
    };
} // namespace std

namespace nex::prudp {

bool packetCmpFunc(const std::shared_ptr<Packet>& lhs, const std::shared_ptr<Packet>& rhs);
using packetCmp = std::integral_constant<decltype(&packetCmpFunc), &packetCmpFunc>;

typedef std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> timePoint;

class PayloadEncoder {
public:
    PayloadEncoder(std::vector<uint8_t> sessionKey, uint8_t maxSubstreamId);
    ~PayloadEncoder() = default;

    std::shared_ptr<Encoder> getReliableEncoder(uint8_t substreamId);
    std::shared_ptr<Encoder> getUnreliableEncoder(const std::shared_ptr<Packet>& packet) const;

    void setReliableEncoder(uint8_t substreamId, std::shared_ptr<Encoder> encoder);
private:
    std::vector<std::shared_ptr<Encoder>> encoders;
    std::vector<uint8_t> sessionKey;

    static std::vector<uint8_t> combineKeys(std::vector<uint8_t> a, std::vector<uint8_t> b);
};

struct Substream {
    uint16_t seqId = 1;
    uint16_t recvSeqId;
    std::set<std::shared_ptr<Packet>, packetCmp> packetQueue;
    std::map<uint16_t, timePoint> nonAckedPackets;
};

struct ClientInfo {
    PayloadEncoder encoder;
    uint8_t minorVersion;
    uint32_t supportedFunctions;
    uint16_t unreliableSeqId;
    std::vector<uint8_t> remoteSignature;
    uint8_t sessionId;
    uint32_t pid;
    std::vector<uint8_t> sessionKey;
    std::vector<Substream> substreams;
    timePoint nextPing;
};

struct DelayedPacket {
    PRUDPAddress addr;
    uint32_t numRetries;
    std::shared_ptr<Packet> packet;
};

struct RMCServerInfo {
    std::function<void()> startFunc;
    std::function<void()> stopFunc;
    std::function<void(PRUDPAddress, uint32_t)> connectFunc;
    std::function<void(PRUDPAddress)> disconnectFunc;
    std::function<void(PRUDPAddress, uint8_t, uint8_t, std::vector<uint8_t>)> dataFunc;
};

class Server {
public:
    Server(std::shared_ptr<Logger::Logger> logger, Logger::group logGroup, std::shared_ptr<SocketManager> socketMgr,
           std::shared_ptr<SettingsManager> settingsMgr, sock::IPv4Addr listenDir, int majorVersion,
           std::vector<uint8_t> accessKey, bool auth, uint32_t pid, std::vector<uint8_t> securePasswd, bool friends,
           uint8_t minorVersion = 4, uint32_t supportedFunctions = 0, uint8_t maxSubstreamId = 0, uint16_t initSeqIdUnreliable = 0);
    ~Server() = default;

    bool listen(const std::function<void()>& closeFunc);
    void stop();

    // Returns the milliseconds until the next delayed packet should be sent.
    uint64_t process();

    void registerRMCServer(uint8_t listenPort, std::function<void()> startFunc, std::function<void()> stopFunc,
                           std::function<void(PRUDPAddress, uint32_t)> connectFunc, std::function<void(PRUDPAddress)> disconnectFunc,
                           std::function<void(PRUDPAddress, uint8_t, uint8_t, std::vector<uint8_t>)> dataFunc);

    void sendDataPacket(PRUDPAddress addr, std::vector<uint8_t> data, uint8_t substreamId = 0);

private:
    int majorVersion;
    std::vector<uint8_t> accessKey;
    bool friends;
    uint8_t minorVersion = 0;
    uint32_t supportedFunctions = 0;
    uint8_t maxSubstreamId = 0;
    uint16_t initSeqIdUnreliable = 0;

    std::shared_ptr<Logger::Logger> logger;
    Logger::group logGroup;

    std::shared_ptr<SettingsManager> settingsMgr;
    std::shared_ptr<SocketManager> socketMgr;

    bool auth;
    uint32_t pid;
    std::vector<uint8_t> securePasswd;

    std::shared_ptr<sock::UDPSocket> socket;
    uint32_t mainSocketID = 0;

    std::unordered_map<PRUDPAddress, ClientInfo> clients;
    std::unordered_map<uint32_t, PRUDPAddress> pidToAddr;
    std::unordered_map<sock::IPv4Addr, sock::IPv4Addr> proxyMap;
    std::recursive_mutex clientsMutex;
    uint8_t nextSessionId = 0;

    std::multimap<timePoint, DelayedPacket> delayedPackets;
    std::recursive_mutex delayedPacketsMutex;

    std::map<uint8_t, RMCServerInfo> registeredServers;

    void onData(sock::IPv4Addr addr, std::vector<uint8_t> data);
    void processPacket(sock::IPv4Addr addr, const std::shared_ptr<Packet>& packet, std::span<uint8_t> data);
    void processPacketQueue(PRUDPAddress prudpAddr, uint8_t substreamId);
    bool handlePacket(PRUDPAddress prudpAddr, const std::shared_ptr<Packet>& packet);
    [[nodiscard]] std::vector<uint8_t> calculateConnSignature(sock::IPv4Addr addr) const;

    void closeClientConnection(PRUDPAddress addr);

    std::shared_ptr<Packet> craftAck(PRUDPAddress addr, const std::shared_ptr<Packet>& req, uint8_t sessionId = 0,
                                     std::vector<uint8_t> remoteSignature = {}, std::vector<uint8_t> sessionKey = {});
    std::shared_ptr<Packet> craftPing(PRUDPAddress addr, uint8_t sessionId, std::vector<uint8_t> remoteSignature,
                                      std::vector<uint8_t> sessionKey);
    std::shared_ptr<Packet> craftAggregateAck(PRUDPAddress addr, uint8_t clientMinor, uint8_t sessionId, uint8_t substreamId,
                                              std::vector<uint16_t> seqIds, std::vector<uint8_t> remoteSignature,
                                              std::vector<uint8_t> sessionKey);

    void sendBytes(sock::IPv4Addr addr, const std::vector<uint8_t>& data);
    void sendPacket(PRUDPAddress addr, const std::shared_ptr<Packet>& packet);
    void resetPingTask(PRUDPAddress addr);
    void deleteNonAckedPacket(PRUDPAddress addr, uint16_t seqId, uint8_t substreamId);

    void logPacket(const std::shared_ptr<Packet>& packet, bool incoming, PRUDPAddress addr);
};

} // namespace nex::prudp

#endif //SPLATOON_SERVER_PRUDP_SERVER_HPP
