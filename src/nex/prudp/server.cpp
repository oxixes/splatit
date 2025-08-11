#include "server.hpp"

#include <utility>
#include <sstream>

#include "../../exceptions.hpp"
#include "../../crypto/tools.hpp"
#include "../../util/util.hpp"
#include "./kerberos.hpp"

namespace nex::prudp {

bool packetCmpFunc(const std::shared_ptr<Packet>& lhs, const std::shared_ptr<Packet>& rhs) {
    return lhs->seqId < rhs->seqId;
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

std::shared_ptr<Encoder> PayloadEncoder::getUnreliableEncoder(const std::shared_ptr<Packet>& packet) const {
    // Since unreliable packets are not ordered, a single ARC4 stream
    // cannot be used, therefore a different one is used for each
    // packet, with a different key depending on the packet.

    const std::vector<uint8_t> constantA = {0x18, 0xd8, 0x23, 0x34, 0x37, 0xe4, 0xe3, 0xfe};
    const std::vector<uint8_t> constantB = {0x23, 0x3e, 0x60, 0x01, 0x23, 0xcd, 0xab, 0x80};

    std::vector<uint8_t> baseKey = combineKeys(sessionKey, constantA);
    std::vector<uint8_t> addition = combineKeys(sessionKey, constantB);
    baseKey.insert(baseKey.end(), addition.begin(), addition.end());

    // The base key is modified according to the packet's data
    baseKey[0] = (baseKey[0] + packet->seqId) & 0xFF;
    baseKey[1] = (baseKey[1] + (packet->seqId >> 8)) & 0xFF;
    baseKey[31] = (baseKey[31] + packet->sessionId) & 0xFF;

    return std::make_shared<Encoder>(baseKey);
}

void PayloadEncoder::setReliableEncoder(uint8_t substreamId, std::shared_ptr<Encoder> encoder) {
    encoders[substreamId] = std::move(encoder);
}

std::vector<uint8_t> PayloadEncoder::combineKeys(std::vector<uint8_t> a, std::vector<uint8_t> b) {
    std::vector<uint8_t> data = std::move(a);
    data.insert(data.end(), b.begin(), b.end());

    return crypto::MD5(data);
}

Server::Server(std::shared_ptr<Logger::Logger> logger, Logger::group logGroup, std::shared_ptr<SocketManager> socketMgr,
               std::shared_ptr<SettingsManager> settingsMgr, sock::IPv4Addr listenDir, int majorVersion,
               std::vector<uint8_t> accessKey, bool auth, uint32_t pid, std::vector<uint8_t> securePasswd, bool friends,
               uint8_t minorVersion, uint32_t supportedFunctions, uint8_t maxSubstreamId, uint16_t initSeqIdUnreliable) {
    this->logger = std::move(logger);
    this->settingsMgr = std::move(settingsMgr);
    this->logGroup = logGroup;
    this->socketMgr = std::move(socketMgr);
    this->auth = auth;
    this->pid = pid;
    this->securePasswd = std::move(securePasswd);

    this->majorVersion = majorVersion;
    this->accessKey = std::move(accessKey);
    this->friends = friends;

    // These only matter if the major version is 1 (PRUDP V1)
    this->minorVersion = (majorVersion == 0) ? 0 : minorVersion;
    this->supportedFunctions = (majorVersion == 0) ? 0 : supportedFunctions;
    this->maxSubstreamId = (majorVersion == 0) ? 0 : maxSubstreamId;
    this->initSeqIdUnreliable = (majorVersion == 0) ? 1 : initSeqIdUnreliable;

    socket = std::make_shared<sock::UDPSocket>();
    socket->setBlocking(false);

    int opt = 1;
    socket->setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address = util::ipv4ToSockAddr(listenDir);
    socket->bind((struct sockaddr*)&address, sizeof(address));
}

void Server::registerRMCServer(uint8_t listenPort, std::function<void()> startFunc, std::function<void()> stopFunc,
                               std::function<void(PRUDPAddress, uint32_t)> connectFunc, std::function<void(PRUDPAddress)> disconnectFunc,
                               std::function<void(PRUDPAddress, uint8_t, uint8_t, std::vector<uint8_t>)> dataFunc) {
    registeredServers.insert({listenPort, RMCServerInfo{std::move(startFunc), std::move(stopFunc),
                                                        std::move(connectFunc), std::move(disconnectFunc),
                                                        std::move(dataFunc)}});
}

bool Server::listen(const std::function<void()>& closeFunc) {
    logger->log(Logger::level::INFO, logGroup, "Starting PRUDP server");

    std::function<void(uint32_t)> closeCallback = nullptr;
    if (closeFunc != nullptr) {
        closeCallback = [closeFunc] (uint32_t) { closeFunc(); };
    }

    for (auto& server : registeredServers) {
        server.second.startFunc();
    }

    mainSocketID = socketMgr->addUDPSocket(socket,
        [&](uint32_t id, const std::vector<uint8_t>& data, sock::IPv4Addr addr){
            if (id != mainSocketID) return;
            onData(addr, data);
        }, closeCallback);

    return true;
}

uint64_t Server::process() {
    std::unique_lock clientsLock(clientsMutex);
    std::unique_lock delayedPacketsLock(delayedPacketsMutex);

    // We have to process all delayed packets, and then return the milliseconds
    // until the next delayed packet should be sent.
    timePoint now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    while (!delayedPackets.empty() && now > delayedPackets.begin()->first) {
        auto packetInfo = delayedPackets.begin()->second;
        delayedPackets.erase(delayedPackets.begin());

        if (packetInfo.numRetries >= MAX_RETRIES) {
            logger->log(Logger::level::DEBUG, logGroup, "Packet to " +
                                                        util::ipv4ToString(packetInfo.addr.address) + ":" +
                                                        std::to_string(packetInfo.addr.address.port) + " timed out");
            closeClientConnection(packetInfo.addr);
            continue;
        }

        if (packetInfo.packet->type == Type::PING && packetInfo.numRetries == 0) {
            // If it's the first time we send this PING packet, we update the sequence ID
            auto it = clients.find(packetInfo.addr);
            if (it != clients.end()) {
                packetInfo.packet->seqId = it->second.unreliableSeqId++;
            }
        }

        logPacket(packetInfo.packet, false, packetInfo.addr);
        sendBytes(packetInfo.addr.address, packetInfo.packet->encode());

        packetInfo.numRetries++;
        auto newTime = now + std::chrono::milliseconds(RETRY_INTERVAL);
        delayedPackets.insert({newTime, packetInfo});

        // We change the timestamp of the packet in the client's data
        // so that we can find it later
        if (packetInfo.packet->type == Type::PING) {
            auto it = clients.find(packetInfo.addr);
            if (it != clients.end()) it->second.nextPing = newTime;
        } else {
            auto it = clients.find(packetInfo.addr);
            if (it != clients.end()) {
                auto reqv1 = (majorVersion == 1) ? std::dynamic_pointer_cast<PacketV1>(packetInfo.packet) : nullptr;
                uint8_t substreamId = (reqv1 == nullptr) ? 0 : reqv1->substreamId;
                auto packetIt = it->second.substreams[substreamId].nonAckedPackets.find(packetInfo.packet->seqId);
                if (packetIt != it->second.substreams[substreamId].nonAckedPackets.end()) packetIt->second = newTime;
            }
        }
    }

    if (delayedPackets.empty()) return UINT64_MAX;
    else return std::chrono::duration_cast<std::chrono::milliseconds>(delayedPackets.begin()->first - now).count();
}

void Server::sendDataPacket(prudp::PRUDPAddress addr, std::vector<uint8_t> data, uint8_t substreamId) {
    std::unique_lock clientsLock(clientsMutex);

    auto it = clients.find(addr);
    if (it == clients.end()) return;

    std::shared_ptr<Packet> packet;
    if (majorVersion == 0) {
        packet = std::make_shared<PacketV0>();
    } else {
        packet = std::make_shared<PacketV1>();
        std::dynamic_pointer_cast<PacketV1>(packet)->substreamId = substreamId;
    }

    packet->accessKey = accessKey;
    packet->type = Type::DATA;
    packet->srcPort = addr.srcVPort;
    packet->srcStreamType = addr.srcStreamType;
    packet->dstPort = addr.vPort;
    packet->dstStreamType = addr.streamType;
    packet->flags = FLAG_RELIABLE | FLAG_NEED_ACK | ((majorVersion == 1) ? FLAG_HAS_SIZE : 0);
    packet->sessionId = it->second.sessionId;
    packet->sessionKey = it->second.sessionKey;
    packet->seqId = it->second.substreams[substreamId].seqId;
    packet->fragmentId = 0;
    packet->connectionSignature = calculateConnSignature(addr.address);
    packet->remoteSignature = it->second.remoteSignature;

    // We increment the sequence ID for the next packet to be sent by the amount of fragments
    // that will be sent, so that the next packet will have the correct sequence ID
    uint16_t fragments = (data.size() + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;
    it->second.substreams[substreamId].seqId += fragments;

    packet->encoder = it->second.encoder.getReliableEncoder((majorVersion == 0) ? 0 : substreamId);
    packet->data = std::move(data);

    sendPacket(addr, packet);
}

void Server::sendBytes(sock::IPv4Addr addr, const std::vector<uint8_t>& data) {
    if (proxyMap.find(addr) != proxyMap.end()) {
        // If the address is in the proxy map, we need to send the data to the proxy
        // instead of the real address. We need to include the proxy header in the data.
        std::vector<uint8_t> newData;
        std::string proxyHeader = "PROXY " + util::ipv4ToString(addr) + ":" + std::to_string(addr.port) + "|";
        newData.insert(newData.end(), proxyHeader.begin(), proxyHeader.end());
        newData.insert(newData.end(), data.begin(), data.end());
        socketMgr->sendto(mainSocketID, newData, proxyMap[addr]);
    } else {
        socketMgr->sendto(mainSocketID, data, addr);
    }
}

void Server::sendPacket(prudp::PRUDPAddress addr, const std::shared_ptr<Packet>& packet) {
    std::vector<std::shared_ptr<Packet>> fragments;

    // If the packet is too big, we need to fragment it
    uint8_t fragmentId = 1;
    auto it = clients.find(addr);
    while (packet->data.size() > MAX_PACKET_SIZE) {
        std::shared_ptr<Packet> fragment = nullptr;
        if (majorVersion == 0) {
            fragment = std::make_shared<PacketV0>(*std::dynamic_pointer_cast<PacketV0>(packet));
        } else {
            fragment = std::make_shared<PacketV1>(*std::dynamic_pointer_cast<PacketV1>(packet));
        }

        fragment->fragmentId = fragmentId++;
        fragment->seqId = packet->seqId++;

        fragment->data = std::vector<uint8_t>(packet->data.begin(), packet->data.begin() + MAX_PACKET_SIZE);
        packet->data.erase(packet->data.begin(), packet->data.begin() + MAX_PACKET_SIZE);

        fragments.push_back(fragment);
    }

    fragments.push_back(packet);

    timePoint sendPoint = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    sendPoint += std::chrono::milliseconds(RETRY_INTERVAL);

    std::unique_lock clientsLock(clientsMutex);
    std::unique_lock delayedPacketsLock(delayedPacketsMutex);

    for (auto& fragment : fragments) {
        if (fragment->flags & FLAG_NEED_ACK) {
            if (!(fragment->flags & FLAG_RELIABLE)) {
                logger->log(Logger::level::WARN, logGroup,
                            "[UNIMPLEMENTED] Sending unreliable packet with FLAG_NEED_ACK. Will not resend if ACK is not received.");
            } else {
                if (it != clients.end()) {
                    auto reqv1 = (majorVersion == 1) ? std::dynamic_pointer_cast<PacketV1>(fragment) : nullptr;
                    uint8_t substreamId = (reqv1 == nullptr) ? 0 : reqv1->substreamId;

                    it->second.substreams[substreamId].nonAckedPackets.insert({fragment->seqId, sendPoint});

                    // We add the packet to the delayed packets queue
                    DelayedPacket packetInfo{addr, 0, fragment};
                    delayedPackets.insert({sendPoint, packetInfo});
                }
            }
        }

        logPacket(fragment, false, addr);
        sendBytes(addr.address, fragment->encode());
    }
}

void Server::onData(sock::IPv4Addr addr, std::vector<uint8_t> data) {
    // Check if the packet comes from a known proxy. The format is 'PROXY <real ip>:<real port>'
    std::string proxyHeader = "PROXY";
    if (data.size() >= proxyHeader.size() && std::equal(proxyHeader.begin(), proxyHeader.end(), data.begin())) {
        if (proxyMap.find(addr) == proxyMap.end()) {
            sock::IPv4Addr proxyAddr = addr;
            proxyAddr.port = 0;
            std::set knownProxies = settingsMgr->getKnownProxies();
            if (knownProxies.find(proxyAddr) == knownProxies.end()) {
                logger->log(Logger::level::WARN, logGroup, "Received packet from unknown proxy " +
                                                           util::ipv4ToString(addr));
                return;
            }

            // Find the '|' character, which separates the proxy header from the actual data
            auto it = std::find(data.begin(), data.end(), '|');
            if (it == data.end()) {
                logger->log(Logger::level::DEBUG, logGroup, "Received packet from proxy with invalid format");
                return;
            }

            // Copy and delete the proxy header
            std::string header(data.begin(), it);
            data.erase(data.begin(), it + 1);

            // Get the real address from the proxy header
            auto addrIt = std::find(header.begin(), header.end(), ' ');
            if (addrIt == header.end()) {
                logger->log(Logger::level::DEBUG, logGroup, "Received packet from proxy with invalid format");
                return;
            }

            std::string realAddrStr(addrIt + 1, header.end());
            auto colonIt = std::find(realAddrStr.begin(), realAddrStr.end(), ':');
            if (colonIt == realAddrStr.end()) {
                logger->log(Logger::level::DEBUG, logGroup, "Received packet from proxy with invalid format");
                return;
            }

            std::string realPortStr(colonIt + 1, realAddrStr.end());
            realAddrStr.erase(colonIt, realAddrStr.end());

            sock::IPv4Addr newAddr = util::stringToIPv4(realAddrStr);
            newAddr.port = std::stoi(realPortStr);

            proxyMap.insert({newAddr, addr});
            addr = newAddr;
        } else {
            addr = proxyMap[addr];
        }
    } else if (proxyMap.find(addr) != proxyMap.end()) {
        proxyMap.erase(addr);
    }

    try {
        // We do this in a loop because there may be multiple packets in a single UDP datagram
        // (only seen in notifications sent from the server to the client when joining a match,
        // but since it's part of the protocol we must handle it).
        while (!data.empty()) {
            // We do a simple parsing here, without checking signatures or decrypting
            // the data. This is because to do so we must first order the packets and
            // assign the data corresponding to each client, which is done in processPacket.
            if (majorVersion == 0) {
                auto packet = std::make_shared<PacketV0>();
                packet->accessKey = accessKey;
                packet->friends = friends;

                size_t size = packet->decode(data);
                processPacket(addr, std::move(packet), data);

                data.erase(data.begin(), data.begin() + (std::ptrdiff_t) size);
            } else {
                auto packet = std::make_shared<PacketV1>();
                packet->accessKey = accessKey;

                size_t size = packet->decode(data);
                processPacket(addr, std::move(packet), data);

                data.erase(data.begin(), data.begin() + (std::ptrdiff_t) size);
            }
        }
    } catch (const MalformedException& e) {
        logger->log(Logger::level::DEBUG, logGroup, "Malformed packet received from " +
            util::ipv4ToString(addr) + ": " + e.what());
    } catch (const std::exception& e) {
        logger->log(Logger::level::FAILURE, logGroup, "Exception while processing packets from " +
            util::ipv4ToString(addr) + ": " + e.what());
    }
}

void Server::processPacket(sock::IPv4Addr addr, const std::shared_ptr<Packet>& packet, std::span<uint8_t> data) {
    PRUDPAddress prudpAddr{addr, packet->srcPort, packet->srcStreamType,
                           packet->dstPort, packet->dstStreamType};

    if (packet->type != Type::SYN) packet->connectionSignature = calculateConnSignature(prudpAddr.address);

    std::unique_lock clientsLock(clientsMutex);

    auto it = clients.find(prudpAddr);
    if (packet->type != Type::SYN && packet->type != Type::CONNECT && it != clients.end()) {
        packet->remoteSignature = it->second.remoteSignature;
        packet->sessionKey = it->second.sessionKey;
    }

    if (!packet->checkSignature()) {
        logger->log(Logger::level::DEBUG, logGroup, "Received packet from " +
                                                    util::ipv4ToString(prudpAddr.address) + " with invalid signature");

        // We log the data in hex for debugging purposes
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t byte : data) {
            ss << std::setw(2) << (int) byte;
        }
        logger->log(Logger::level::DEBUG, logGroup, "Data: " + ss.str());
        return;
    }

    logPacket(packet, true, prudpAddr);

    if (packet->type == Type::SYN || packet->type == Type::CONNECT) {
        handlePacket(prudpAddr, packet);
        return;
    }

    if (it != clients.end()) {
        // Check if the substream ID is valid
        std::shared_ptr<PacketV1> packetv1 = nullptr;
        if (majorVersion == 1) {
            packetv1 = std::dynamic_pointer_cast<PacketV1>(packet);
            if ((packetv1->substreamId > maxSubstreamId && !(packetv1->flags & FLAG_MULTI_ACK)) ||
                (packetv1->flags & FLAG_MULTI_ACK && packetv1->substreamId != maxSubstreamId + 1)) {
                logger->log(Logger::level::DEBUG, logGroup, "Received packet from " +
                                                            util::ipv4ToString(prudpAddr.address) + " with invalid substream ID");
                return;
            }
        }

        // Firstly, we check if the packet is reliable, if it's not we can
        // process it immediately, otherwise we need to add it to the queue
        // and check if there are not any missing packets, in which case we
        // can process them.
        if (!(packet->flags & FLAG_RELIABLE)) {
            if (packet->fragmentId != 0 && !(packet->flags & FLAG_ACK)) {
                logger->log(Logger::level::WARN, logGroup, "[UNIMPLEMENTED] Received unreliable fragmented packet from " +
                                                            util::ipv4ToString(prudpAddr.address));
                return;
            }

            bool isMultiAck = packet->flags & FLAG_MULTI_ACK;

            if (!isMultiAck) packet->encoder = it->second.encoder.getUnreliableEncoder(packet);
            packet->decryptData(isMultiAck);

            handlePacket(prudpAddr, packet);
            resetPingTask(prudpAddr);
            return;
        }

        // Make sure we don't have too many packets in the queue, otherwise we
        // may run out of memory, so we'll destroy the client, as it's probably
        // a malicious client trying to flood us.
        uint8_t substreamId = (packetv1 == nullptr) ? 0 : packetv1->substreamId;
        if (it->second.substreams[substreamId].packetQueue.size() > MAX_PACKET_QUEUE_SIZE) {
            logger->log(Logger::level::WARN, logGroup, "Client " + util::ipv4ToString(prudpAddr.address)
                                                        + ":" + std::to_string(prudpAddr.vPort) + " sent too many packets");
            closeClientConnection(prudpAddr);
            return;
        }

        // TODO Send aggregate ACKs instead of plain ACKs if the client supports it
        // If we have already processed the packet, we can just send
        // an ACK and ignore it. This happens when an ACK is sent, but
        // the client does not receive it, so it sends the packet again.
        if (it->second.substreams[substreamId].recvSeqId >= packet->seqId) {
            auto res = craftAck(prudpAddr, packet, it->second.sessionId, it->second.remoteSignature,
                                it->second.sessionKey);
            sendPacket(prudpAddr, res);
            resetPingTask(prudpAddr);
            return;
        }

        // Add it to the queue and check if there are not any missing packets,
        // in which case we can process them.
        it->second.substreams[substreamId].packetQueue.insert(packet);
        processPacketQueue(prudpAddr, substreamId);

        resetPingTask(prudpAddr);
    } else {
        // If not, ignore the packet
        logger->log(Logger::level::DEBUG, logGroup, "Received packet from unknown client " +
        util::ipv4ToString(addr) + " with type " + std::to_string((int) packet->type));
    }
}

void Server::processPacketQueue(prudp::PRUDPAddress prudpAddr, uint8_t substreamId) {
    std::unique_lock clientsLock(clientsMutex);

    auto it = clients.find(prudpAddr);
    if (it == clients.end()) return;

    auto& packetQueue = it->second.substreams[substreamId].packetQueue;

    bool supportsAggregateAck = majorVersion == 1 && it->second.minorVersion >= 1;
    std::vector<uint16_t> ackedSeqIds;

    bool disconnected = false;

    // We need to check if there are not any missing packets, in which case
    // we can process them.
    auto packetIt = packetQueue.begin();
    while (packetIt != packetQueue.end()) {
        // If the packet is not the next one, we can't process it yet
        // (some packets are missing)
        if (packetIt->get()->seqId != it->second.substreams[substreamId].recvSeqId + 1) break;

        // Check the packet fragments (if any) to see if the packet is complete

        auto fragmentIt = packetIt;
        bool fragmentComplete = false;

        // The first fragment ID should be 1, and the last one should be 0
        // Therefore, the first fragment ID should be 1 (there are more fragments) or 0
        // (there is only one fragment), any other value means that the packet is not
        // complete
        if (fragmentIt->get()->fragmentId != 0 && fragmentIt->get()->fragmentId != 1) break;

        bool multipleFragments = false;
        while (!fragmentComplete) {
            // Send ACKs for all the fragments we have received and has not been ACKed yet
            if (fragmentIt->get()->seqId > it->second.substreams[substreamId].recvSeqId) {
                if (fragmentIt->get()->flags & FLAG_NEED_ACK) {
                    if (supportsAggregateAck && fragmentIt->get()->type == Type::DATA) {
                        ackedSeqIds.push_back(fragmentIt->get()->seqId);
                    } else {
                        auto res = craftAck(prudpAddr, *fragmentIt, it->second.sessionId, it->second.remoteSignature,
                                            it->second.sessionKey);

                        // If the packet is a disconnect and needs ACK, we send 3 ACK, so that the client
                        // receives at least one of them, if it does not need it, we just close the connection.
                        for (int i = 0; i < (fragmentIt->get()->type == Type::DISCONNECT ? 3 : 1); i++) {
                            sendPacket(prudpAddr, res);
                        }
                    }
                }

                it->second.substreams[substreamId].recvSeqId++;
            }

            // If the fragment ID is 0, it's the last fragment, so we can
            // process it immediately
            if (fragmentIt->get()->fragmentId == 0) {
                fragmentComplete = true;
            } else {
                // If it's not the last fragment, we need to check if the next
                // fragment is in the queue, if it's not, we can't process it yet
                if (std::next(fragmentIt) == packetQueue.end()) break;
                if (std::next(fragmentIt)->get()->seqId != fragmentIt->get()->seqId + 1 ||
                    (std::next(fragmentIt)->get()->fragmentId != fragmentIt->get()->fragmentId + 1 &&
                    std::next(fragmentIt)->get()->fragmentId != 0)) break;

                multipleFragments = true;
                fragmentIt++;
            }
        }

        if (!fragmentComplete) break;

        // By this point, the packet should be complete, so we can process it

        auto encoder = it->second.encoder.getReliableEncoder(substreamId);

        std::vector<uint8_t> data;
        fragmentIt = packetIt;

        std::shared_ptr<Packet> lastFragment = nullptr;
        do {
            fragmentIt->get()->encoder = encoder;
            fragmentIt->get()->decryptData();

            if (fragmentIt->get()->fragmentId == 0) lastFragment = *fragmentIt;

            if (multipleFragments) data.insert(data.end(), fragmentIt->get()->data.begin(), fragmentIt->get()->data.end());
            fragmentIt++;
        } while (fragmentIt != packetQueue.end() && fragmentIt->get()->fragmentId != 0);

        // Prevent copying the data if there is only one fragment
        if (multipleFragments) lastFragment->data = std::move(data);

        try {
            if (lastFragment->type == Type::DISCONNECT && supportsAggregateAck) {
                disconnected = true;

                if (!ackedSeqIds.empty()) {
                    // Since the client is disconnecting, we will send all the
                    // remaining acks.
                    auto res = craftAggregateAck(prudpAddr, it->second.minorVersion, it->second.sessionId,
                                                 substreamId, ackedSeqIds, it->second.remoteSignature,
                                                 it->second.sessionKey);
                    sendPacket(prudpAddr, res);
                }
            }
            if (!handlePacket(prudpAddr, lastFragment)) break;
        } catch (const std::exception& e) {
            // Send all the acks we have processed so far
            if (supportsAggregateAck && !ackedSeqIds.empty()) {
                auto res = craftAggregateAck(prudpAddr, it->second.minorVersion, it->second.sessionId,
                                             substreamId, ackedSeqIds, it->second.remoteSignature,
                                             it->second.sessionKey);
                sendPacket(prudpAddr, res);
            }

            // We remove the packet from the queue, as an error occurred
            packetQueue.erase(packetIt, fragmentIt);
            throw e;
        }

        // If the packet was processed successfully, we can remove it
        // from the queue
        packetIt = packetQueue.erase(packetIt, fragmentIt);
    }

    // If the client supports aggregate acknowledgements, send one for
    // all the processed packets of the queue instead of one for each packet.
    if (supportsAggregateAck && !disconnected && !ackedSeqIds.empty()) {
        auto res = craftAggregateAck(prudpAddr, it->second.minorVersion, it->second.sessionId,
                                     substreamId, ackedSeqIds, it->second.remoteSignature,
                                     it->second.sessionKey);
        sendPacket(prudpAddr, res);
    }
}

bool Server::handlePacket(prudp::PRUDPAddress prudpAddr, const std::shared_ptr<Packet>& packet) {
    std::unique_lock clientsLock(clientsMutex);

    auto it = clients.find(prudpAddr);

    if (packet->flags & FLAG_ACK || packet->flags & FLAG_MULTI_ACK) {
        if (it == clients.end()) return true;

        if (!(packet->flags & FLAG_MULTI_ACK)) {
            // The ping task will be already reset by the caller
            if (packet->type == Type::PING) return true;

            auto reqv1 = (majorVersion == 1) ? std::dynamic_pointer_cast<PacketV1>(packet) : nullptr;
            uint8_t substreamId = (reqv1 == nullptr) ? 0 : reqv1->substreamId;
            deleteNonAckedPacket(prudpAddr, packet->seqId, substreamId);
        } else {
            if (majorVersion == 0 || it->second.minorVersion < 1 || packet->type != Type::DATA) return true;

            // We handle the aggregate ACK here
            uint16_t ackUpTo = 0;
            std::vector<uint16_t> extraAcks;
            uint8_t substreamId = 0;

            auto& data = packet->data;

            if (minorVersion >= 2) {
                // New version aggregate ACK
                if (data.size() < 4) return false;
                substreamId = data[0];
                if (substreamId > maxSubstreamId) return false;

                uint8_t numAcks = data[1];
                if (data.size() != 4 + numAcks * 2) return false;
                ackUpTo = data[2] | (data[3] << 8);
                for (uint8_t i = 0; i < numAcks; i++) {
                    extraAcks.push_back(data[4 + i * 2] | (data[5 + i * 2] << 8));
                }
            } else {
                // Old version aggregate ACK
                if (data.size() % 2 != 0) return false;
                ackUpTo = packet->seqId;
                substreamId = std::dynamic_pointer_cast<PacketV1>(packet)->substreamId;
                for (size_t i = 0; i < data.size() / 2; i++) {
                    extraAcks.push_back(data[i * 2] | (data[i * 2 + 1] << 8));
                }
            }

            auto& nonAckPackets = it->second.substreams[substreamId].nonAckedPackets;
            while (!nonAckPackets.empty() && nonAckPackets.begin()->first <= ackUpTo) {
                deleteNonAckedPacket(prudpAddr, nonAckPackets.begin()->first, substreamId);
            }

            for (auto seqId : extraAcks) {
                deleteNonAckedPacket(prudpAddr, seqId, substreamId);
            }
        }

        return true;
    }

    if (packet->type == Type::SYN) {
        // If type is SYN, reply with a SYN-ACK, but we'll not
        // add the client to the list yet, as that is done with
        // the CONNECT packet.

        auto res = craftAck(prudpAddr, packet);
        sendPacket(prudpAddr, res);
        return true;
    } else if (packet->type == Type::CONNECT) {
        // If the type is CONNECT, check if the provided data is
        // correct and add the client to the list.

        std::shared_ptr<PacketV1> reqv1 = nullptr;
        if (majorVersion == 1) {
            reqv1 = std::dynamic_pointer_cast<PacketV1>(packet);
            if (reqv1->minorVersion > minorVersion) {
                logger->log(Logger::level::DEBUG, logGroup, "Received CONNECT packet from " +
                                                            util::ipv4ToString(prudpAddr.address) + " with invalid minor version");
                return false;
            }

            if (reqv1->supportedFunctions & ~supportedFunctions) {
                logger->log(Logger::level::DEBUG, logGroup, "Received CONNECT packet from " +
                                                            util::ipv4ToString(prudpAddr.address) + " with invalid supported functions");
                return false;
            }

            if (reqv1->maxSubstreamId != maxSubstreamId) {
                logger->log(Logger::level::DEBUG, logGroup, "Received CONNECT packet from " +
                                                            util::ipv4ToString(prudpAddr.address) + " with invalid max substream ID");
                return false;
            }
        }

        packet->decryptData(true); // Just copy the encrypted data to the decrypted data
                                         // as it's not really encrypted like the rest of the
                                         // packets.

        // The default key is always CD&ML
        std::vector<uint8_t> key = {'C', 'D', '&', 'M', 'L'};
        uint32_t userPid = 0;
        uint32_t responseCheckValue = 0;
        uint32_t connectionId = 0;
        try {
            if (!auth) kerberos::decryptConnectRequest(packet->data, securePasswd, pid,
                                                       key, userPid, connectionId, responseCheckValue);
        } catch (const std::runtime_error& e) {
            logger->log(Logger::level::INFO, logGroup, "Failed to authenticate client " +
                                                        util::ipv4ToString(prudpAddr.address) + ":" +
                                                        std::to_string(prudpAddr.address.port) + ": " + e.what());
            return false;
        } catch (const std::exception& e) {
            logger->log(Logger::level::FAILURE, logGroup, "Exception while authenticating client " +
                                                        util::ipv4ToString(prudpAddr.address) + ":" +
                                                        std::to_string(prudpAddr.address.port) + ": " + e.what());
            return false;
        }

        if (it != clients.end() || (userPid != 0 && userPid != 100 && pidToAddr.contains(userPid)))
            closeClientConnection(pidToAddr.contains(userPid) ? pidToAddr.at(userPid) : prudpAddr);

        timePoint now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
        timePoint nextPing = now + std::chrono::milliseconds(PING_INTERVAL);

        clients.insert({prudpAddr, {
                PayloadEncoder(key, maxSubstreamId),
                reqv1 != nullptr ? reqv1->minorVersion : static_cast<uint8_t>(0),
                supportedFunctions,
                initSeqIdUnreliable,
                packet->remoteSignature,
                nextSessionId,
                userPid,
                (auth) ? std::vector<uint8_t>() : key,
                std::vector<Substream>(maxSubstreamId + 1, {
                        1,
                        0,
                        std::set<std::shared_ptr<Packet>, packetCmp>()
                }),
                nextPing
        }});

        if (userPid != 0 && userPid != 100) pidToAddr.insert({userPid, prudpAddr});

        auto rmcIt = registeredServers.find(packet->dstPort);
        if (rmcIt != registeredServers.end()) {
            clientsLock.unlock();
            rmcIt->second.connectFunc(prudpAddr, userPid);
            clientsLock.lock();
        }

        // The connect packet is the one with seqId 1
        clients.find(prudpAddr)->second.substreams[(reqv1 != nullptr) ? reqv1->substreamId : 0].recvSeqId = 1;

        auto pingPacket = craftPing(prudpAddr, nextSessionId, packet->remoteSignature,
                                    (auth) ? std::vector<uint8_t>() : key);

        std::unique_lock delayedPacketsLock(delayedPacketsMutex);

        delayedPackets.insert({nextPing, {
                prudpAddr,
                0,
                std::move(pingPacket)
        }});

        auto res= craftAck(prudpAddr, packet, nextSessionId, packet->remoteSignature);

        if (!auth) {
            responseCheckValue++;
            res->data = {0x04, 0x00, 0x00, 0x00};
            util::getu32Little(responseCheckValue);
            res->data.insert(res->data.end(), (uint8_t*) &responseCheckValue, (uint8_t*) &responseCheckValue + 4);
        }

        sendPacket(prudpAddr, res);

        nextSessionId++;

        logger->log(Logger::level::INFO, logGroup, "Client " + util::ipv4ToString(prudpAddr.address)
                                                + ":" + std::to_string(prudpAddr.address.port) + " connected");
        return true;
    } else if (packet->type == Type::DISCONNECT) {
        closeClientConnection(prudpAddr);
        return false;
    } else if (packet->type == Type::PING) {
        // We just have to reply with an ACK if the packet is needs ack,
        // otherwise we just ignore it (although it does not make to send
        // a ping without requiring ack, but we'll follow the protocol).
        if (packet->flags & FLAG_NEED_ACK && !(packet->flags & FLAG_RELIABLE)) {
            auto res = craftAck(prudpAddr, packet, it->second.sessionId, it->second.remoteSignature,
                                it->second.sessionKey);
            sendPacket(prudpAddr, res);
        }
    } else if (packet->type == Type::DATA) {
        // We need to process the packet first, and only then send the ACK,
        // otherwise the packet may be incomplete, and we would have already
        // sent the ACK, so the client would not send the packet again.

        auto res = craftAck(prudpAddr, packet, it->second.sessionId, it->second.remoteSignature,
                            it->second.sessionKey);

        auto rmcInfo = registeredServers.find(packet->dstPort);
        if (rmcInfo == registeredServers.end()) {
            logger->log(Logger::level::DEBUG, logGroup, "Received DATA packet from " +
                                                        util::ipv4ToString(prudpAddr.address) + " with invalid port");
        } else {
            uint8_t substreamId = (majorVersion == 0) ? 0 : std::dynamic_pointer_cast<PacketV1>(packet)->substreamId;
            uint8_t packetMinorVersion = it->second.minorVersion;
            clientsLock.unlock();

            rmcInfo->second.dataFunc(prudpAddr, packetMinorVersion, substreamId, packet->data);
        }
    }

    return true;
}

void Server::closeClientConnection(prudp::PRUDPAddress addr) {
    auto rmcIt = registeredServers.find(addr.srcVPort);
    if (rmcIt != registeredServers.end()) {
        rmcIt->second.disconnectFunc(addr);
    }

    std::unique_lock clientsLock(clientsMutex);
    std::unique_lock delayedPacketsLock(delayedPacketsMutex);

    auto it = clients.find(addr);
    if (it == clients.end()) return;

    if (it->second.pid != 0) pidToAddr.erase(it->second.pid);

    auto nextPing = it->second.nextPing;

    std::vector<timePoint> tasksToDelete = {nextPing};

    for (auto& substream : it->second.substreams) {
        for (auto& packet : substream.nonAckedPackets) {
            tasksToDelete.push_back(packet.second);
        }
    }

    for (auto& task : tasksToDelete) {
        // There could be more than one task at the same time, so we need to check
        // all of them.
        auto taskItL = delayedPackets.lower_bound(task);
        auto taskItU = delayedPackets.upper_bound(task);

        while (taskItL != taskItU) {
            if (taskItL->second.addr == addr) {
                taskItL = delayedPackets.erase(taskItL);
            } else {
                taskItL++;
            }
        }
    }

    clients.erase(it);

    logger->log(Logger::level::INFO, logGroup, "Client " + util::ipv4ToString(addr.address)
                                                + ":" + std::to_string(addr.address.port) + " disconnected");
}

std::shared_ptr<Packet> Server::craftAck(prudp::PRUDPAddress addr, const std::shared_ptr<Packet>& req, uint8_t sessionId,
                                         std::vector<uint8_t> remoteSignature, std::vector<uint8_t> sessionKey) {
    std::shared_ptr<Packet> res;
    if (majorVersion == 0) {
        res = std::make_shared<PacketV0>();
        std::dynamic_pointer_cast<PacketV0>(res)->friends = friends;
    } else {
        res = std::make_shared<PacketV1>();
    }

    if (req->type == Type::SYN || req->type == Type::CONNECT) {
        if (majorVersion == 1) {
            auto packet = std::dynamic_pointer_cast<PacketV1>(res);
            auto reqv1 = std::dynamic_pointer_cast<PacketV1>(req);

            packet->substreamId = reqv1->substreamId;
            packet->minorVersion = (req->type == Type::SYN) ? (reqv1->minorVersion > minorVersion) ? minorVersion : reqv1->minorVersion
                                                            : reqv1->minorVersion;
            packet->supportedFunctions = supportedFunctions;
            packet->maxSubstreamId = maxSubstreamId;
            packet->initSeqIdUnreliable = initSeqIdUnreliable;
        }
    }

    res->accessKey = accessKey;
    res->type = req->type;
    res->srcPort = req->dstPort;
    res->srcStreamType = req->dstStreamType;
    res->dstPort = req->srcPort;
    res->dstStreamType = req->srcStreamType;
    res->flags = FLAG_ACK | ((minorVersion == 1) ? FLAG_HAS_SIZE : 0);
    res->sessionId = (req->type == Type::SYN) ? 0 : sessionId;
    if (req->type != Type::SYN && req->type != Type::CONNECT) res->sessionKey = std::move(sessionKey);
    res->seqId = req->seqId;
    res->fragmentId = 0;
    res->connectionSignature = (req->type == Type::SYN) ? calculateConnSignature(addr.address) :
                               (majorVersion == 0) ? std::vector<uint8_t>(4, 0) : std::vector<uint8_t>(16, 0);

    auto connSign = calculateConnSignature(addr.address);
    if (req->type == Type::SYN) {
        // Print
        std::stringstream ss;
        for (uint8_t byte : connSign) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int) byte;
        }
    }

    if (req->type != Type::SYN) res->remoteSignature = std::move(remoteSignature);

    return res;
}

std::shared_ptr<Packet> Server::craftPing(PRUDPAddress addr, uint8_t sessionId, std::vector<uint8_t> remoteSignature,
                                          std::vector<uint8_t> sessionKey) {

    std::shared_ptr<Packet> res;
    if (majorVersion == 0) {
        res = std::make_shared<PacketV0>();
        std::dynamic_pointer_cast<PacketV0>(res)->friends = friends;
    } else {
        res = std::make_shared<PacketV1>();
    }

    res->accessKey = accessKey;
    res->type = Type::PING;
    res->srcPort = addr.srcVPort;
    res->srcStreamType = addr.srcStreamType;
    res->dstPort = addr.vPort;
    res->dstStreamType = addr.streamType;
    res->flags = (majorVersion == 0) ? FLAG_NEED_ACK : FLAG_NEED_ACK | FLAG_HAS_SIZE;
    res->sessionId = sessionId;
    res->sessionKey = std::move(sessionKey);
    res->seqId = 1;
    res->fragmentId = 0;
    res->connectionSignature = calculateConnSignature(addr.address);
    res->remoteSignature = std::move(remoteSignature);

    return res;
}

std::shared_ptr<Packet> Server::craftAggregateAck(PRUDPAddress addr, uint8_t clientMinor, uint8_t sessionId, uint8_t substreamId,
                                                  std::vector<uint16_t> seqIds, std::vector<uint8_t> remoteSignature,
                                                  std::vector<uint8_t> sessionKey) {
    // Aggregate acks are only supported in PRUDP v1
    auto res = std::make_shared<PacketV1>();
    res->substreamId = (clientMinor >= 2) ? maxSubstreamId + 1 : substreamId;
    res->accessKey = accessKey;
    res->type = Type::DATA;
    res->srcPort = addr.srcVPort;
    res->srcStreamType = addr.srcStreamType;
    res->dstPort = addr.vPort;
    res->dstStreamType = addr.streamType;
    res->flags = FLAG_MULTI_ACK | FLAG_HAS_SIZE;
    res->sessionId = sessionId;
    res->sessionKey = std::move(sessionKey);
    res->seqId = (clientMinor >= 2) ? 0 : seqIds.back();
    res->fragmentId = 0;
    res->connectionSignature = calculateConnSignature(addr.address);
    res->remoteSignature = std::move(remoteSignature);

    if (clientMinor >= 2) {
        std::vector<uint8_t> data = {substreamId, 0, (uint8_t) (seqIds.back() & 0xFF), (uint8_t) (seqIds.back() >> 8)};
        res->data = std::move(data);
    }

    return res;
}

void Server::resetPingTask(prudp::PRUDPAddress addr) {
    std::unique_lock clientsLock(clientsMutex);
    std::unique_lock delayedPacketsLock(delayedPacketsMutex);

    auto it = clients.find(addr);
    if (it == clients.end()) return;

    // Find and delete the delayed packet corresponding to the ping task
    auto taskItL = delayedPackets.lower_bound(it->second.nextPing);
    auto taskItU = delayedPackets.upper_bound(it->second.nextPing);

    DelayedPacket pingTask;

    while (taskItL != taskItU) {
        if (taskItL->second.addr == addr && taskItL->second.packet->type == Type::PING) {
            pingTask = taskItL->second;
            delayedPackets.erase(taskItL);
            break;
        } else {
            taskItL++;
        }
    }

    if (pingTask.numRetries > 0) pingTask.packet->seqId++;

    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    it->second.nextPing = now + std::chrono::milliseconds(PING_INTERVAL);

    pingTask.numRetries = 0;
    delayedPackets.insert({it->second.nextPing, pingTask});
}

void Server::deleteNonAckedPacket(prudp::PRUDPAddress addr, uint16_t seqId, uint8_t substreamId) {
    std::unique_lock clientsLock(clientsMutex);
    std::unique_lock delayedPacketsLock(delayedPacketsMutex);

    auto it = clients.find(addr);
    if (it == clients.end()) return;

    auto packetIt = it->second.substreams[substreamId].nonAckedPackets.find(seqId);

    if (packetIt == it->second.substreams[substreamId].nonAckedPackets.end()) return;
    timePoint task = packetIt->second;
    it->second.substreams[substreamId].nonAckedPackets.erase(packetIt);

    auto taskItL = delayedPackets.lower_bound(task);
    auto taskItU = delayedPackets.upper_bound(task);

    while (taskItL != taskItU) {
        if (taskItL->second.addr == addr && taskItL->second.packet->seqId == seqId) {
            taskItL = delayedPackets.erase(taskItL);
        } else {
            taskItL++;
        }
    }
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
    for (auto& server : registeredServers) {
        server.second.stopFunc();
    }

    logger->log(Logger::level::INFO, logGroup, "Stopping PRUDP server");

    std::unique_lock clientsLock(clientsMutex);
    std::unique_lock delayedPacketsLock(delayedPacketsMutex);

    clients.clear();
    delayedPackets.clear();

    socketMgr->close(mainSocketID, true);
    socket = nullptr;
}

void Server::logPacket(const std::shared_ptr<Packet>& packet, bool incoming, PRUDPAddress addr) {
    std::string typeStr = "UNKNOWN";

    if (packet->type == Type::PING) return;

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
    // MULTI_ACK is only used in DATA packets,
    // and we'll treat it as if it was a different type
    // since it can acknowledge multiple packets
    // (of a possible different type) at once
    if (packet->type == Type::DATA && packet->flags & FLAG_MULTI_ACK) typeStr = "MULTI ACK";

    // Pad the string to 14 characters
    typeStr.resize(14, ' ');

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
    sessionIdStr.resize(12, ' ');

    std::string sizeStr = "Size: " + std::to_string(packet->size());

    std::string output = "[PRUDP] [" + util::ipv4ToString(addr.address) + ":" + std::to_string(addr.address.port) + "] ";
    output += (incoming) ? "<- " : "-> ";
    output += typeStr + " - " + flagsStr + " | " + seqIdStr + " | " + fragmentIdStr + " | " + sessionIdStr + " | " + sizeStr;


    logger->log(Logger::level::DEBUG, logGroup, output);
}

} // namespace nex::prudp