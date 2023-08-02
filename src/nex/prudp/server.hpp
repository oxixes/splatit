#ifndef SPLATOON_SERVER_PRUDP_SERVER_HPP
#define SPLATOON_SERVER_PRUDP_SERVER_HPP

#include <memory>
#include <set>

#include "../../socket/socket.hpp"
#include "packet.hpp"

namespace prudp {

struct PRUDPAddress {
    sock::IPv4Addr address;
    uint8_t vPort;
    uint8_t streamType; // This will be used to differentiate clients (ignored otherwise),
                        // although in practice it will always be 0xA. I don't know the differences
                        // between other stream types, as they've not been documented.
};

bool packetCmpFunc(const Packet& lhs, const Packet& rhs) {
    if (lhs.seqId != rhs.seqId) return lhs.seqId < rhs.seqId;
    // Fragment IDs start at 1 and increment by 1 for each fragment,
    // but 0 marks the last fragment, so we need to handle that case.
    if (lhs.fragmentId == 0) return false;
    else if (rhs.fragmentId == 0) return true;
    else return lhs.fragmentId < rhs.fragmentId;
}
using packetCmp = std::integral_constant<decltype(&packetCmpFunc), &packetCmpFunc>;

struct ClientInfo {
    std::shared_ptr<Encoder> encoder;
    uint8_t minorVersion;
    uint16_t seqId;
    uint16_t recvSeqId;
    std::vector<uint8_t> senderSignature;
    std::vector<uint8_t> connectionSignature;
    std::set<Packet, packetCmp> packetQueue;
};

class Server {
public:
    Server() = default;
    ~Server() = default;

    bool init();
    void cleanup();

    void process();

private:

};

} // namespace prudp

#endif //SPLATOON_SERVER_PRUDP_SERVER_HPP
