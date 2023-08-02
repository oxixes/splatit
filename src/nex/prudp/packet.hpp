#ifndef SPLATOON_SERVER_PACKET_HPP
#define SPLATOON_SERVER_PACKET_HPP

#include <cstdint>
#include <vector>
#include <memory>
#include <openssl/evp.h>

#include "../../crypto/arc4.hpp"

namespace prudp {

class MalformedException : public std::runtime_error {
public:
    explicit MalformedException(const std::string& what_arg) : std::runtime_error(what_arg) {};
    explicit MalformedException(const char* what_arg) : std::runtime_error(what_arg) {};
};

enum class Type {
    SYN = 0,
    CONNECT = 1,
    DATA = 2,
    DISCONNECT = 3,
    PING = 4
};

#define FLAG_ACK 0x01
#define FLAG_RELIABLE 0x02
#define FLAG_NEED_ACK 0x04
#define FLAG_HAS_SIZE 0x08
#define FLAG_MULTI_ACK 0x200

class Encoder {
public:
    explicit Encoder(const std::vector<uint8_t>& key);
    ~Encoder() = default;

    std::vector<uint8_t> encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decode(const std::vector<uint8_t>& data);

private:
    ARC4 encCtx;
    ARC4 decCtx;
};

class Packet {
public:
    Type type;
    uint8_t srcStreamType = 0xA; // The stream type is always 0xA (RVSecure) for NEX
    uint8_t srcPort;
    uint8_t dstStreamType = 0xA;
    uint8_t dstPort;
    uint16_t flags;
    uint8_t sessionId;
    uint16_t seqId;
    uint8_t fragmentId;
    std::vector<uint8_t> sessionKey;
    std::vector<uint8_t> connectionSignature;
    std::vector<uint8_t> senderSignature;
    std::vector<uint8_t> accessKey;

    std::shared_ptr<Encoder> encoder = nullptr;

    std::vector<uint8_t> data;

    virtual std::vector<uint8_t> encode() = 0;
    virtual size_t decode(const std::vector<uint8_t>& data) = 0;
    void decryptData();
    virtual bool checkSignature() = 0;

protected:
    std::vector<uint8_t> encryptedData;
    std::vector<uint8_t> signature;

    void encryptData();
};

class PacketV0 : public Packet {
public:
    bool friends = true;

    std::vector<uint8_t> encode() override;
    size_t decode(const std::vector<uint8_t>& data) override;
    bool checkSignature() override;

private:
    std::vector<uint8_t> calculateSignature(const std::vector<uint8_t>& senderSignature);
    static uint8_t calculateChecksum(const std::vector<uint8_t>& data, const std::vector<uint8_t>& accessKey);
};

class PacketV1 : public Packet {
public:
    uint8_t substreamId = 0;
    uint8_t minorVersion = 4; // https://github.com/kinnay/NintendoClients/wiki/PRUDP-Protocol#supported-functions
    uint32_t supportedFunctions = 0;
    uint8_t maxSubstreamId = 0;
    uint16_t initSeqIdUnreliable = 0;

    std::vector<uint8_t> encode() override;
    size_t decode(const std::vector<uint8_t>& data) override;
    bool checkSignature() override;

private:
    std::vector<uint8_t> packetSpecificData;
    std::vector<uint8_t> header;

    std::vector<uint8_t> calculateSignature(const std::vector<uint8_t>& packet,
                                            const std::vector<uint8_t>& senderSignature,
                                            const std::vector<uint8_t>& pSpecificData);
};

} // namespace prudp

#endif //SPLATOON_SERVER_PACKET_HPP
