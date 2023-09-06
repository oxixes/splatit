#ifndef SPLATOON_SERVER_RMC_REQUEST_HPP
#define SPLATOON_SERVER_RMC_REQUEST_HPP

#include "types.hpp"

namespace nex::rmc {

// This class is NOT used for encoding/decoding the request params,
// that is done in the with the ParamParser class.
class Request {
public:
    Request() = default;

    [[nodiscard]] std::vector<uint8_t> encode(size_t paramLength) const;
    std::vector<uint8_t> decode(std::vector<uint8_t> data);

    uint8_t protocolId = 0;
    uint16_t extendedProtocolId = 0;
    uint32_t callId = 0;
    uint32_t methodId = 0;
};

}

#endif //SPLATOON_SERVER_RMC_REQUEST_HPP
