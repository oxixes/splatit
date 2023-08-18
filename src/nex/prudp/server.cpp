#include "server.hpp"

#include <utility>

#include "../../crypto/tools.hpp"
#include "../../util/util.hpp"

namespace prudp {

bool packetCmpFunc(const std::unique_ptr<Packet>& lhs, const std::unique_ptr<Packet>& rhs) {
    if (lhs->seqId != rhs->seqId) return lhs->seqId < rhs->seqId;
    // Fragment IDs start at 1 and increment by 1 for each fragment,
    // but 0 marks the last fragment, so we need to handle that case.
    if (lhs->fragmentId == 0) return false;
    else if (rhs->fragmentId == 0) return true;
    else return lhs->fragmentId < rhs->fragmentId;
}

PayloadEncoder::PayloadEncoder(std::vector<uint8_t> sessionKey, uint8_t maxSubstreamId) {
    this->sessionKey = sessionKey;

    for (uint8_t i = 0; i <= maxSubstreamId; i++) {
        encoders.push_back(std::make_shared<Encoder>(sessionKey));

        // The key is modified for every substream after the first one
        size_t add = (size_t) (sessionKey.size() / 2) + 1;
        for (size_t j = 0; j < sessionKey.size() / 2; j++) {
            sessionKey[j] += (sessionKey[j] + add - j) & 0xFF;
        }
    }
}

std::shared_ptr<Encoder> PayloadEncoder::getReliableEncoder(uint8_t substreamId) {
    return encoders[substreamId];
}

std::shared_ptr<Encoder> PayloadEncoder::getUnreliableEncoder(const std::unique_ptr<Packet>& packet) {
    // Since unreliable packets are not ordered, a single ARC4 stream
    // cannot be used, therefore a different one is used for each
    // packet, with a different key depending on the packet.

    std::vector<uint8_t> constantA = {0x18, 0xd8, 0x23, 0x34, 0x37, 0xe4, 0xe3, 0xfe};
    std::vector<uint8_t> constantB = {0x23, 0x3e, 0x60, 0x01, 0x23, 0xcd, 0xab, 0x80};

    std::vector<uint8_t> baseKey = combineKeys(sessionKey, constantA);
    std::vector<uint8_t> addition = combineKeys(sessionKey, constantB);
    baseKey.insert(baseKey.end(), addition.begin(), addition.end());

    // The base key is modified according to the packet's data
    baseKey[0] = (baseKey[0] + packet->seqId) & 0xFF;
    baseKey[1] = (baseKey[1] + (packet->seqId >> 8)) & 0xFF;
    baseKey[31] = (baseKey[31] + packet->sessionId) & 0xFF;

    return std::make_shared<Encoder>(baseKey);
}

std::vector<uint8_t> PayloadEncoder::combineKeys(std::vector<uint8_t> a, std::vector<uint8_t> b) {
    std::vector<uint8_t> data = std::move(a);
    data.insert(data.end(), b.begin(), b.end());

    return crypto::MD5(data);
}

Server::Server(std::shared_ptr<Logger::Logger> logger, Logger::group logGroup, std::shared_ptr<SocketManager> socketMgr,
               sock::IPv4Addr listenDir, int majorVersion, std::vector<uint8_t> accessKey, bool auth, uint32_t pid,
               std::vector<uint8_t> securePasswd, bool friends, uint8_t minorVersion, uint32_t supportedFunctions,
               uint8_t maxSubstreamId, uint16_t initSeqIdUnreliable) {
    this->logger = std::move(logger);
    this->logGroup = logGroup;
    this->socketMgr = std::move(socketMgr);
    this->auth = auth;
    this->pid = pid;
    this->securePasswd = std::move(securePasswd);

    this->majorVersion = majorVersion;
    this->accessKey = std::move(accessKey);
    this->friends = friends;
    this->minorVersion = minorVersion;
    this->supportedFunctions = supportedFunctions;
    this->maxSubstreamId = maxSubstreamId;
    this->initSeqIdUnreliable = initSeqIdUnreliable;

    socket = std::make_shared<sock::UDPSocket>();
    socket->setBlocking(false);

    int opt = 1;
    socket->setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address = util::ipv4ToSockAddr(listenDir);
    socket->bind((struct sockaddr*)&address, sizeof(address));
}

bool Server::listen(const std::function<void()>& closeFunc) {
    logger->log(Logger::level::INFO, logGroup, "Starting PRUDP server");

    std::function<void(uint32_t)> closeCallback = nullptr;
    if (closeFunc != nullptr) {
        closeCallback = [closeFunc] (uint32_t) { closeFunc(); };
    }

    mainSocketID = socketMgr->addUDPSocket(socket,
        [&](uint32_t id, const std::vector<uint8_t>& data, sock::IPv4Addr addr){
            if (id != mainSocketID) return;
            onData(addr, data);
        }, closeCallback);

    return true;
}

void Server::onData(sock::IPv4Addr addr, std::vector<uint8_t> data) {
    try {
        // We do this in a loop because there may be multiple packets in a single UDP datagram
        // (only seen in notifications sent from the server to the client when joining a match,
        // but since it's part of the protocol we must handle it).
        while (!data.empty()) {
            // We do a simple parsing here, without checking signatures or decrypting
            // the data. This is because to do so we must first order the packets and
            // assign the data corresponding to each client, which is done in processPacket.
            if (majorVersion == 0) {
                auto packet = std::make_unique<PacketV0>();
                packet->accessKey = accessKey;
                packet->friends = friends;

                size_t size = packet->decode(data);
                data.erase(data.begin(), data.begin() + (long long) size);

                processPacket(addr, std::move(packet));
            } else {
                auto packet = std::make_unique<PacketV1>();
                packet->accessKey = accessKey;

                size_t size = packet->decode(data);
                data.erase(data.begin(), data.begin() + (long long) size);

                processPacket(addr, std::move(packet));
            }
        }
    } catch (const MalformedException& e) {
        logger->log(Logger::level::DEBUG, logGroup, "Malformed packet received from " +
            util::ipv4ToString(addr) + ": " + e.what());
    } catch (const std::exception& e) {
        logger->log(Logger::level::WARN, logGroup, "Exception while processing packet from " +
            util::ipv4ToString(addr) + ": " + e.what());
    }
}

void Server::processPacket(sock::IPv4Addr addr, std::unique_ptr<Packet> packet) {
    PRUDPAddress prudpAddr{addr, packet->srcPort, packet->srcStreamType};

    // Check if we already have a client with this address
    auto it = clients.find(prudpAddr);
    if (it == clients.end()) {
        // If not, check if this is a connection request (SYN or CONNECT)
        if (packet->type == Type::SYN) {
            // If type is SYN, reply with a SYN-ACK, but we'll not
            // add the client to the list yet, as that is done with
            // the CONNECT packet.

            //packet->connectionSignature = std::vector<uint8_t>((majorVersion == 0) ? 4 : 16, 0);
            if (!packet->checkSignature()) {
                logger->log(Logger::level::DEBUG, logGroup, "Received SYN packet from " +
                    util::ipv4ToString(addr) + " with invalid signature");
                return;
            }

            logPacket(packet, true, prudpAddr);

            auto res = craftSynAck(prudpAddr, std::move(packet));
            logPacket(res, false, prudpAddr);
            socketMgr->sendto(mainSocketID, res->encode(), addr);
        } else if (packet->type == Type::CONNECT) {
            // If the type is CONNECT, check if the provided data is
            // correct and add the client to the list.

            // The default key is always CD&ML

            if (auth) {

            } else {
                // TODO Check ticket to validate user and get session key
            }
        } else {
            // If not, ignore the packet
            logger->log(Logger::level::DEBUG, logGroup, "Received packet from unknown client " +
            util::ipv4ToString(addr) + " with type " + std::to_string((int) packet->type));
        }
    } else {
        // Make sure the packet is not a connection request (ignore it if it is)
        // TODO

        // Make sure we don't have too many packets in the queue, otherwise we
        // may run out of memory, so we'll destroy the client, as it's probably
        // a malicious client trying to flood us.
        // TODO

        // Add it to the queue and check if there are not any missing packets,
        // in which case we can process them.
        // TODO
    }
}

std::unique_ptr<Packet> Server::craftSynAck(prudp::PRUDPAddress addr, std::unique_ptr<Packet> req) {
    std::unique_ptr<Packet> res;
    if (majorVersion == 0) {
        auto packet = std::make_unique<PacketV0>();
        packet->friends = friends;

        res = std::move(packet);
    } else {
        auto packet = std::make_unique<PacketV1>();
        packet->substreamId = 0;
        auto reqv1 = dynamic_cast<PacketV1*>(req.get());
        packet->minorVersion = (minorVersion > reqv1->minorVersion) ? reqv1->minorVersion : minorVersion;
        packet->supportedFunctions = supportedFunctions;
        packet->maxSubstreamId = maxSubstreamId;
        packet->initSeqIdUnreliable = initSeqIdUnreliable;

        res = std::move(packet);
    }

    res->accessKey = accessKey;
    res->type = Type::SYN;
    res->srcPort = req->dstPort;
    res->srcStreamType = req->dstStreamType;
    res->dstPort = req->srcPort;
    res->dstStreamType = req->srcStreamType;
    res->flags = FLAG_ACK | FLAG_HAS_SIZE;
    res->sessionId = 0;
    res->seqId = 0;
    res->fragmentId = 0;
    res->connectionSignature = calculateConnSignature(addr.address);
    // res->remoteSignature = std::vector<uint8_t>((majorVersion == 0) ? 4 : 16, 0);

    return res;
}

std::vector<uint8_t> Server::calculateConnSignature(sock::IPv4Addr addr) const {
    std::vector<uint8_t> data(6);
    data[0] = addr.a;
    data[1] = addr.b;
    data[2] = addr.c;
    data[3] = addr.d;
    data[4] = addr.port & 0xFF;
    data[5] = addr.port >> 8;

    std::vector<uint8_t> signature = crypto::MD5(data);
    if (majorVersion == 0) signature.resize(4);

    return signature;
}

void Server::stop() {
    socketMgr->close(mainSocketID, true);
    socket = nullptr;
}

void Server::logPacket(const std::unique_ptr<Packet>& packet, bool incoming, PRUDPAddress addr) {
    std::string typeStr = "UNKNOWN";
    switch (packet->type) {
        case Type::SYN:
            typeStr = "SYN";
            break;
        case Type::CONNECT:
            typeStr = "CONNECT";
            break;
        case Type::DATA:
            typeStr = "DATA";
            break;
        case Type::DISCONNECT:
            typeStr = "DISCONNECT";
            break;
        case Type::PING:
            typeStr = "PING";
            break;
    }

    if (packet->flags & FLAG_ACK) typeStr += " ACK";
    // MULTI_ACK is only used in DATA packets
    // and we'll treat it as if it was a different type
    // since it can acknowledge multiple packets
    // (of a possible different type) at once
    if (packet->type == Type::DATA && packet->flags & FLAG_MULTI_ACK) typeStr = "MULTI ACK";

    // Pad the string to 10 characters
    typeStr.resize(10, ' ');

    std::string flagsStr;
    if (packet->flags & FLAG_RELIABLE) flagsStr += "R";
    if (packet->flags & FLAG_NEED_ACK) flagsStr += "N";
    if (packet->flags & FLAG_HAS_SIZE) flagsStr += "S";

    // Pad the string to 3 characters
    flagsStr.resize(3, ' ');

    std::string seqIdStr = "Seq: " + std::to_string(packet->seqId);
    seqIdStr.resize(8, ' ');

    std::string fragmentIdStr = "Frag: " + std::to_string(packet->fragmentId);
    fragmentIdStr.resize(8, ' ');

    std::string sessionIdStr = "Session: " + std::to_string(packet->sessionId);
    sessionIdStr.resize(11, ' ');

    std::string sizeStr = "Size: " + std::to_string(packet->data.size());

    std::string output = "[PRUDP] [" + util::ipv4ToString(addr.address) + ":" + std::to_string(addr.address.port) + "] ";
    output += (incoming) ? "<- " : "-> ";
    output += typeStr + " - " + flagsStr + " | " + seqIdStr + " | " + fragmentIdStr + " | " + sessionIdStr + " | " + sizeStr;

    logger->log(Logger::level::DEBUG, logGroup, output);
}

} // namespace prudp