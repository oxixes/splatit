#include "request.hpp"

#include "../../util/util.hpp"
#include "../../exceptions.hpp"

namespace nex::rmc {

std::vector<uint8_t> Request::encode(size_t paramLength) const {
    std::vector<uint8_t> data;
    for (int i = 0; i < 4; i++) data.push_back(0); // Reserve space for the length
    data.push_back(protocolId | 0x80);

    if (protocolId == 0x7F) {
        data.push_back(extendedProtocolId & 0xFF);
        data.push_back(extendedProtocolId >> 8);
    }

    uint32_t temp = callId;
    util::getu32Little(temp); // Convert to little endian if host is big endian
    data.insert(data.end(), (uint8_t*)&temp, (uint8_t*)&temp + 4);

    temp = methodId;
    util::getu32Little(temp);
    data.insert(data.end(), (uint8_t*)&temp, (uint8_t*)&temp + 4);

    // Write the length
    uint32_t length = data.size() - 4 + paramLength; // Does not include the length itself
    util::getu32Little(length);
    data[0] = length & 0xFF;
    data[1] = (length >> 8) & 0xFF;
    data[2] = (length >> 16) & 0xFF;
    data[3] = (length >> 24) & 0xFF;

    return data;
}

std::vector<uint8_t> Request::decode(std::vector<uint8_t> data) {
    // Special exception thrown to notify the underlying PRUDP server that the packet, despite
    // looking complete, is in fact missing fragments
    if (data.size() < 13) throw NotCompleteException("Invalid request length");

    uint32_t length = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    if (length != data.size() - 4) throw NotCompleteException("Invalid request length");
    if (length < 9) throw MalformedException("Invalid request length");

    protocolId = data[4] & 0x7F;
    if (protocolId == 0x7F) {
        if (length < 11) throw MalformedException("Invalid request length");
        extendedProtocolId = data[5] | (data[6] << 8);
    }

    data.erase(data.begin(), data.begin() + (protocolId == 0x7F ? 7 : 5));

    callId = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    methodId = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);

    data.erase(data.begin(), data.begin() + 8);

    if (protocolId == 3 && methodId == 3) {
        // Print the data in hex bytes
        printf("Data: ");
        for (int i = 0; i < data.size(); i++) {
            printf("%02X", data[i]);
        }
        printf("\n");
    }

    return data;
}

} // namespace nex::rmc