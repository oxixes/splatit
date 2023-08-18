#ifndef SPLATOON_SERVER_PRUDP_SERVER_HPP
#define SPLATOON_SERVER_PRUDP_SERVER_HPP

#include <memory>
#include <set>
#include <unordered_map>

#include "../../socket/socket.hpp"
#include "../../logger.hpp"
#include "../../socket/socketManager.hpp"
#include "packet.hpp"

namespace prudp {

    struct PRUDPAddress {
        sock::IPv4Addr address;
        uint8_t vPort;
        uint8_t streamType; // This will be used to differentiate clients (ignored otherwise),
                            // although in practice it will always be 0xA. I don't know the differences
                            // between other stream types, as they've not been documented.

        bool operator ==(const PRUDPAddress& other) const {
            return address == other.address && vPort == other.vPort && streamType == other.streamType;
        }
    };

} // namespace prudp

// Create a hash function for PRUDPAddress, so we can use it as a key in an unordered_map.
namespace std {
    template<>
    struct hash<prudp::PRUDPAddress> {
        std::size_t operator()(const prudp::PRUDPAddress& addr) const {
            size_t h1 = hash<sock::IPv4Addr>()(addr.address);
            size_t h2 = hash<uint8_t>()(addr.vPort);
            size_t h3 = hash<uint8_t>()(addr.streamType);

            // Combine hashes (This is the way Boost combines hashes)
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2))
                   ^ (h3 + 0x9e3779b9 + (h2 << 6) + (h2 >> 2));
        }
    };
} // namespace std

namespace prudp {

bool packetCmpFunc(const std::unique_ptr<Packet>& lhs, const std::unique_ptr<Packet>&& rhs);
using packetCmp = std::integral_constant<decltype(&packetCmpFunc), &packetCmpFunc>;

class PayloadEncoder {
public:
    PayloadEncoder(std::vector<uint8_t> sessionKey, uint8_t maxSubstreamId);
    ~PayloadEncoder() = default;

    std::shared_ptr<Encoder> getReliableEncoder(uint8_t substreamId);
    std::shared_ptr<Encoder> getUnreliableEncoder(const std::unique_ptr<Packet>& packet);
private:
    std::vector<std::shared_ptr<Encoder>> encoders;
    std::vector<uint8_t> sessionKey;

    static std::vector<uint8_t> combineKeys(std::vector<uint8_t> a, std::vector<uint8_t> b);
};

struct Substream {
    uint16_t seqId;
    uint16_t recvSeqId;
    std::set<std::unique_ptr<Packet>, packetCmp> packetQueue;
};

struct ClientInfo {
    PayloadEncoder encoder;
    uint8_t minorVersion;
    uint32_t supportedFunctions;
    uint16_t unreliableSeqId;
    std::vector<uint8_t> remoteSignature;
    std::vector<uint8_t> connectionSignature;
    std::vector<uint8_t> sessionKey;
    std::vector<Substream> substreams;
};

class Server {
public:
    Server(std::shared_ptr<Logger::Logger> logger, Logger::group logGroup, std::shared_ptr<SocketManager> socketMgr,
           sock::IPv4Addr listenDir, int majorVersion, std::vector<uint8_t> accessKey, bool auth, uint32_t pid,
           std::vector<uint8_t> securePasswd, bool friends, uint8_t minorVersion = 4, uint32_t supportedFunctions = 0,
           uint8_t maxSubstreamId = 0, uint16_t initSeqIdUnreliable = 0);
    ~Server() = default;

    bool listen(const std::function<void()>& closeFunc);
    void stop();

    void process();

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

    std::shared_ptr<SocketManager> socketMgr;

    bool auth;
    uint32_t pid;
    std::vector<uint8_t> securePasswd;

    std::shared_ptr<sock::UDPSocket> socket;
    uint32_t mainSocketID = 0;

    std::unordered_map<PRUDPAddress, ClientInfo> clients;

    void onData(sock::IPv4Addr addr, std::vector<uint8_t> data);
    void processPacket(sock::IPv4Addr addr, std::unique_ptr<Packet> packet);
    [[nodiscard]] std::vector<uint8_t> calculateConnSignature(sock::IPv4Addr addr) const;

    std::unique_ptr<Packet> craftSynAck(PRUDPAddress addr, std::unique_ptr<Packet> req);

    void logPacket(const std::unique_ptr<Packet>& packet, bool incoming, PRUDPAddress addr);
};

} // namespace prudp

#endif //SPLATOON_SERVER_PRUDP_SERVER_HPP
