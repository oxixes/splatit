#include "response.hpp"
#include "../../util/util.hpp"
#include "../../exceptions.hpp"

namespace nex::rmc {

std::vector<uint8_t> Response::encode(size_t paramLength) const {
    std::vector<uint8_t> data;

    for (int unsigned i = 0; i < 4; i++) data.push_back(0); // Reserve space for the length
    data.push_back(protocolId);
    if (protocolId == 0x7F) {
        data.push_back(extendedProtocolId & 0xFF);
        data.push_back(extendedProtocolId >> 8);
    }

    data.push_back(success ? 1 : 0);

    if (success) {
        uint32_t temp = callId;
        util::getu32Little(temp); // Convert to little endian if host is big endian
        data.insert(data.end(), (uint8_t*)&temp, (uint8_t*)&temp + 4);

        temp = methodId | 0x8000;
        util::getu32Little(temp);
        data.insert(data.end(), (uint8_t*)&temp, (uint8_t*)&temp + 4);
    } else {
        auto temp = (uint32_t) error;
        temp |= 0x80000000;
        util::getu32Little(temp);
        data.insert(data.end(), (uint8_t*)&temp, (uint8_t*)&temp + 4);

        temp = callId;
        util::getu32Little(temp);
        data.insert(data.end(), (uint8_t*)&temp, (uint8_t*)&temp + 4);
    }

    // Write the length
    uint32_t length = data.size() - 4 + paramLength; // Does not include the length itself
    util::getu32Little(length);
    data[0] = length & 0xFF;
    data[1] = (length >> 8) & 0xFF;
    data[2] = (length >> 16) & 0xFF;
    data[3] = (length >> 24) & 0xFF;

    return data;
}

std::vector<uint8_t> Response::decode(std::vector<uint8_t> data) {
    // Special exception thrown to notify the underlying PRUDP server that the packet, despite
    // looking complete, is in fact missing fragments
    if (data.size() < 14) throw NotCompleteException("Invalid request length");

    uint32_t length = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    if (length != data.size() - 4) throw NotCompleteException("Invalid request length");
    if (length < 10) throw MalformedException("Invalid request length");

    protocolId = data[4];
    if (protocolId == 0x7F) {
        if (length < 12) throw MalformedException("Invalid request length");
        extendedProtocolId = data[5] | (data[6] << 8);
    }

    data.erase(data.begin(), data.begin() + (protocolId == 0x7F ? 7 : 5));

    success = data[0] == 1;

    if (success) {
        callId = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
        methodId = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);
        methodId &= 0x7FFF;
    } else {
        error = (Error) (data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24));
        callId = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);
    }

    data.erase(data.begin(), data.begin() + 9);

    return data;
}

} // namespace nex::rmc