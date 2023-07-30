#include "packet.hpp"
#include "../../util/util.hpp"
#include "../../crypto/tools.hpp"

namespace prudp {

Encoder::Encoder(const std::vector<uint8_t>& key) : encCtx(key), decCtx(key) {}

std::vector<uint8_t> Encoder::encode(const std::vector<uint8_t>& data) {
    return encCtx.encrypt(data);
}

std::vector<uint8_t> Encoder::decode(const std::vector<uint8_t>& data) {
    return decCtx.decrypt(data);
}

void Packet::encryptData() {
    if (encoder == nullptr)
        throw std::runtime_error("Encoder is null");

    encryptedData = encoder->encode(data);
}

void Packet::decryptData() {
    if (encoder == nullptr)
        throw std::runtime_error("Encoder is null");

    data = encoder->decode(encryptedData);
}

std::vector<uint8_t> PacketV0::encode() {
    std::vector<uint8_t> result;

    result.push_back(srcPort | srcStreamType << 4); // Source
    result.push_back(dstPort | dstStreamType << 4); // Destination

    uint16_t flagsAndType = static_cast<uint16_t>(type) | (flags << 4);
    result.push_back(flagsAndType & 0xFF); // Flags and type
    result.push_back((flagsAndType >> 8) & 0xFF);

    result.push_back(sessionId); // Session ID

    if (!data.empty()) encryptData(); // Encrypt the data (needed for the signature)

    // Add the sender signature
    std::vector<uint8_t> signature = calculateSignature(senderSignature);

    result.insert(result.end(), signature.begin(), signature.end());

    result.push_back(seqId & 0xFF); // Sequence ID
    result.push_back((seqId >> 8) & 0xFF);

    // Get the packet specific data
    if (type == Type::SYN || type == Type::CONNECT) {
        result.insert(result.end(), connectionSignature.begin(), connectionSignature.end());
    } else if (type == Type::DATA) {
        result.push_back(fragmentId);
    }

    if (flags & FLAG_HAS_SIZE) {
        auto size = static_cast<uint16_t>(encryptedData.size());
        result.push_back(size & 0xFF);
        result.push_back((size >> 8) & 0xFF);
    }

    // Add the encrypted data
    result.insert(result.end(), encryptedData.begin(), encryptedData.end());

    // Finally, add the checksum
    uint8_t checksum = calculateChecksum(result, accessKey);
    result.push_back(checksum);

    return result;
}

size_t PacketV0::decode(const std::vector<uint8_t>& data) {
    // 12 bytes is the minimum size of a packet
    if (data.size() < 12)
        throw MalformedException("Packet is too small (invalid size)");

    // Analyze the header
    srcPort = data[0] & 0xF;
    srcStreamType = data[0] >> 4;
    dstPort = data[1] & 0xF;
    dstStreamType = data[1] >> 4;
    uint16_t flagsAndType = data[2] | (data[3] << 8);
    flags = flagsAndType >> 4;
    if ((flagsAndType & 0xF) > 4) // 4 is the highest type
        throw MalformedException("Invalid type");
    type = static_cast<Type>(flagsAndType & 0xF);
    sessionId = data[4];

    std::vector<uint8_t> signature(data.begin() + 5, data.begin() + 9);

    seqId = data[9] | (data[10] << 8);

    // Analyze the packet specific data
    size_t offset;
    if (type == Type::SYN || type == Type::CONNECT) {
        if (data.size() < 15)
            throw MalformedException("Packet is too small (invalid size)");
        senderSignature = std::vector<uint8_t>(data.begin() + 11, data.begin() + 15);
        offset = 15;
    } else if (type == Type::DATA) {
        if (data.size() < 13)
            throw MalformedException("Packet is too small (invalid size)");
        fragmentId = data[11];
        offset = 12;
    }

    if (flags & FLAG_HAS_SIZE) {
        // -1 because of the checksum
        if (data.size() - 1 < offset + 2)
            throw MalformedException("Packet is too small (invalid size)");

        uint16_t size = data[offset] | (data[offset + 1] << 8);
        offset += 2;

        if (data.size() - offset - 1 != size)
            throw MalformedException("Specified size does not match the actual size of the data");

        if (size > 0) encryptedData = std::vector<uint8_t>(data.begin() + (long long) offset,
                                             data.begin() + (long long) offset + size);
        offset += size;
    } else if (data.size() - 1 > offset) {
        encryptedData = std::vector<uint8_t>(data.begin() + (long long) offset, data.end() - 1);
        offset += encryptedData.size();
    }

    // Check the checksum
    std::vector<uint8_t> dataCopy(data.begin(), data.begin() + (long long) offset); // Remove checksum
    uint8_t checksum = data.back();
    if (checksum != calculateChecksum(dataCopy, accessKey))
        throw MalformedException("Invalid checksum");

    // Check the signature
    std::vector<uint8_t> calculatedSignature = calculateSignature(connectionSignature);
    if (calculatedSignature != signature)
        throw MalformedException("Invalid signature");

    return offset + 1;
}

std::vector<uint8_t> PacketV0::calculateSignature(const std::vector<uint8_t>& senderSignature) {
    std::vector<uint8_t> signature;

    if (friends) {
        // This is a special case for the signature
        // used in the friends server, which is the only
        // one where V0 packets are used in the Wii U
        if (type == Type::DATA && encryptedData.empty()) {
            signature = {0x12, 0x34, 0x56, 0x78};
        } else if (type == Type::DATA) {
            std::vector<uint8_t> keyHash = crypto::MD5(accessKey);
            std::vector<uint8_t> hmac = crypto::HMAC_MD5(keyHash, encryptedData);
            signature = {hmac[0], hmac[1], hmac[2], hmac[3]};
        } else {
            signature = senderSignature;
        }
    } else {
        // This signature is used in games using V0 packets
        // which in practice is only on the 3DS, but we'll
        // write it to recreate the whole protocol
        if (type == Type::DATA || type == Type::DISCONNECT) {
            std::vector<uint8_t> keyHash = crypto::MD5(accessKey);
            std::vector<uint8_t> data;
            data.insert(data.end(), sessionKey.begin(), sessionKey.end());
            data.push_back(seqId & 0xFF);
            data.push_back((seqId >> 8) & 0xFF);
            data.push_back(fragmentId);
            data.insert(data.end(), encryptedData.begin(), encryptedData.end());
            std::vector<uint8_t> hmac = crypto::HMAC_MD5(keyHash, data);
            signature = {hmac[0], hmac[1], hmac[2], hmac[3]};
        } else {
            signature = senderSignature;
        }
    }

    return signature;
}

uint8_t PacketV0::calculateChecksum(const std::vector<uint8_t>& data, const std::vector<uint8_t>& accessKey) {
    uint64_t sum = 0;
    for (size_t i = 0; i < data.size() / 4; i += 1) {
        uint32_t word = data[i * 4] | (data[i * 4 + 1] << 8) | (data[i * 4 + 2] << 16) | (data[i * 4 + 3] << 24);
        sum += word;
    }

    uint64_t result = 0;
    for (auto byte : accessKey) result += byte;
    for (size_t i = data.size() & ~3; i < data.size(); i += 1) result += data[i];
    uint32_t sum32 = sum & 0xFFFFFFFF;
    for (int i = 0; i < 4; i += 1) result += *((uint8_t*)&sum32 + i);

    return result & 0xFF;
}

std::vector<uint8_t> PacketV1::encode() {
    std::vector<uint8_t> result {0xEA, 0xD0}; // Magic number

    // Craft the packet-specific data
    std::vector<uint8_t> packetSpecificData;
    if (type == Type::SYN || type == Type::CONNECT) {
        uint32_t supportedFunctionsField = (supportedFunctions << 8) | minorVersion;
        util::getu32Little(supportedFunctionsField);
        packetSpecificData.push_back(0); // Option ID
        packetSpecificData.push_back(4); // Option length
        packetSpecificData.insert(packetSpecificData.end(),
                                  (uint8_t*)&supportedFunctionsField, (uint8_t*)&supportedFunctionsField + 4);

        if (connectionSignature.size() != 16 && !connectionSignature.empty())
            throw std::runtime_error("Invalid connection signature length");
        packetSpecificData.push_back(1); // Option ID
        if (connectionSignature.empty()) {
            packetSpecificData.push_back(16); // Option length
            // Fill with zeroes
            for (int i = 0; i < 16; i += 1) packetSpecificData.push_back(0);
        } else {
            packetSpecificData.push_back(connectionSignature.size()); // Option length
            packetSpecificData.insert(packetSpecificData.end(), connectionSignature.begin(), connectionSignature.end());
        }
        packetSpecificData.push_back(connectionSignature.size()); // Option length
        packetSpecificData.insert(packetSpecificData.end(), connectionSignature.begin(), connectionSignature.end());

        if (type == Type::CONNECT) {
            packetSpecificData.push_back(3); // Option ID
            packetSpecificData.push_back(2); // Option length
            packetSpecificData.push_back(initSeqIdUnreliable & 0xFF);
            packetSpecificData.push_back((initSeqIdUnreliable >> 8) & 0xFF);
        }

        packetSpecificData.push_back(4); // Option ID
        packetSpecificData.push_back(1); // Option length
        packetSpecificData.push_back(maxSubstreamId);
    } else if (type == Type::DATA) {
        packetSpecificData.push_back(2); // Option ID
        packetSpecificData.push_back(1); // Option length
        packetSpecificData.push_back(fragmentId);
    }

    // Encrypt the payload
    if (!data.empty()) encryptData();

    // Add the header
    result.push_back(0x01); // Version
    result.push_back(packetSpecificData.size()); // Packet specific data length
    uint16_t payloadSize = encryptedData.size();
    result.push_back(payloadSize & 0xFF); // Payload size
    result.push_back((payloadSize >> 8) & 0xFF);
    result.push_back(srcPort | srcStreamType << 4); // Source
    result.push_back(dstPort | dstStreamType << 4); // Destination
    uint16_t flagsAndType = static_cast<uint16_t>(type) | (flags << 4);
    result.push_back(flagsAndType & 0xFF); // Flags and type
    result.push_back((flagsAndType >> 8) & 0xFF);
    result.push_back(sessionId); // Session ID
    result.push_back(substreamId); // Substream ID
    result.push_back(seqId & 0xFF); // Sequence ID
    result.push_back((seqId >> 8) & 0xFF);

    // Add the signature
    std::vector<uint8_t> signature = calculateSignature(result, senderSignature, packetSpecificData);
    result.insert(result.end(), signature.begin(), signature.end());

    // Add the packet-specific data
    result.insert(result.end(), packetSpecificData.begin(), packetSpecificData.end());

    // Add the payload
    result.insert(result.end(), encryptedData.begin(), encryptedData.end());

    return result;
}

size_t PacketV1::decode(const std::vector<uint8_t>& data) {
    if (data.size() < 30) throw std::runtime_error("Packet is too small (invalid size)"); // 30 = magic + header + signature
    if (data[0] != 0xEA || data[1] != 0xD0) throw std::runtime_error("Invalid magic number");
    if (data[2] != 0x01) throw std::runtime_error("Invalid major version");

    uint8_t packetSpecificDataLength = data[3];
    uint16_t payloadSize = data[4] | (data[5] << 8);
    if (data.size() < 30 + packetSpecificDataLength + payloadSize)
        throw std::runtime_error("Packet is too small (invalid size)");

    // Analyze the header
    srcPort = data[6] & 0xF;
    srcStreamType = data[6] >> 4;
    dstPort = data[7] & 0xF;
    dstStreamType = data[7] >> 4;
    uint16_t flagsAndType = data[8] | (data[9] << 8);
    flags = flagsAndType >> 4;
    if ((flagsAndType & 0xF) > 4) // 4 is the highest type
        throw MalformedException("Invalid type");
    type = static_cast<Type>(flagsAndType & 0xF);
    sessionId = data[10];
    substreamId = data[11];
    seqId = data[12] | (data[13] << 8);

    // Get the payload
    encryptedData = std::vector<uint8_t>(data.begin() + 30 + packetSpecificDataLength,
                                         data.begin() + 30 + packetSpecificDataLength + payloadSize);

    // Check the signature
    std::vector<uint8_t> signature(data.begin() + 14, data.begin() + 30);
    std::vector<uint8_t> packetSpecificData(data.begin() + 30, data.begin() + 30 + packetSpecificDataLength);
    std::vector<uint8_t> calculatedSignature = calculateSignature(data, connectionSignature, packetSpecificData);
    if (signature != calculatedSignature) throw MalformedException("Invalid signature");

    // Analyze the packet-specific data
    size_t offset = 0;
    while (offset < packetSpecificData.size()) {
        uint8_t optionId = packetSpecificData[offset++];
        uint8_t optionLength = packetSpecificData[offset++];
        if (offset + optionLength > packetSpecificData.size())
            throw MalformedException("Invalid packet-specific data length");

        switch (optionId) {
            case 0: {
                if (optionLength != 4) throw MalformedException("Invalid option length");
                if (type != Type::SYN && type != Type::CONNECT)
                    throw MalformedException("Invalid option ID (" + std::to_string(optionId)
                                    + ") for this packet type (" + std::to_string(static_cast<int>(type)) + ")");
                uint32_t supportedFunctionsField = packetSpecificData[offset] | (packetSpecificData[offset + 1] << 8) |
                                                   (packetSpecificData[offset + 2] << 16) |
                                                   (packetSpecificData[offset + 3] << 24);
                supportedFunctions = supportedFunctionsField >> 8;
                minorVersion = supportedFunctionsField & 0xFF;
                break;
            }
            case 1: {
                if (optionLength != 16) throw MalformedException("Invalid option length");
                if (type != Type::SYN && type != Type::CONNECT)
                    throw MalformedException("Invalid option ID (" + std::to_string(optionId)
                                    + ") for this packet type (" + std::to_string(static_cast<int>(type)) + ")");
                senderSignature = std::vector<uint8_t>(packetSpecificData.begin() + (long long) offset,
                                                           packetSpecificData.begin() + (long long) offset + optionLength);
                break;
            }
            case 2: {
                if (optionLength != 1) throw MalformedException("Invalid option length");
                if (type != Type::DATA) throw MalformedException("Invalid option ID (" + std::to_string(optionId)
                                    + ") for this packet type (" + std::to_string(static_cast<int>(type)) + ")");
                fragmentId = packetSpecificData[offset];
                break;
            }
            case 3: {
                if (optionLength != 2) throw MalformedException("Invalid option length");
                if (type != Type::CONNECT) throw MalformedException("Invalid option ID (" + std::to_string(optionId)
                                    + ") for this packet type (" + std::to_string(static_cast<int>(type)) + ")");
                initSeqIdUnreliable = packetSpecificData[offset] | (packetSpecificData[offset + 1] << 8);
                break;
            }
            case 4: {
                if (optionLength != 1) throw MalformedException("Invalid option length");
                if (type != Type::SYN && type != Type::CONNECT)
                    throw MalformedException("Invalid option ID (" + std::to_string(optionId)
                                    + ") for this packet type (" + std::to_string(static_cast<int>(type)) + ")");
                maxSubstreamId = packetSpecificData[offset];
                break;
            }
            default:
                throw MalformedException("Invalid option ID");
        }

        offset += optionLength;
    }

    return 30 + packetSpecificDataLength + payloadSize;
}

std::vector<uint8_t> PacketV1::calculateSignature(const std::vector<uint8_t>& packet, const std::vector<uint8_t>& senderSignature,
                             const std::vector<uint8_t>& packetSpecificData) {
    std::vector<uint8_t> data(packet.begin() + 6, packet.begin() + 14);
    if (!sessionKey.empty())
        data.insert(data.end(), sessionKey.begin(), sessionKey.end());

    uint32_t accessKeySum = 0;
    for (auto byte : accessKey) accessKeySum += byte;
    util::getu32Little(accessKeySum);
    data.insert(data.end(), (uint8_t*)&accessKeySum, (uint8_t*)&accessKeySum + 4);

    if (!senderSignature.empty())
        data.insert(data.end(), senderSignature.begin(), senderSignature.end());

    data.insert(data.end(), packetSpecificData.begin(), packetSpecificData.end());
    data.insert(data.end(), encryptedData.begin(), encryptedData.end());

    std::vector<uint8_t> keyHash = crypto::MD5(accessKey);
    return crypto::HMAC_MD5(keyHash, data);
}

} // namespace prudp