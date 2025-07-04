#ifndef SPLATOON_SERVER_RMC_RESPONSE_HPP
#define SPLATOON_SERVER_RMC_RESPONSE_HPP

#include "types.hpp"
#include "errors.hpp"

namespace nex::rmc {

// This class is NOT used for encoding/decoding the response params,
// that is done in the with the ParamParser class.
class Response {
public:
    Response() = default;
    Response(const Response& other) = default;
    Response(Response&& other) noexcept = default;
    ~Response() = default;

    [[nodiscard]] std::vector<uint8_t> encode(size_t paramLength) const;
    std::vector<uint8_t> decode(std::vector<uint8_t> data);

    Response& operator=(const Response& other) = default;
    Response& operator=(Response&& other) noexcept = default;

    uint8_t protocolId = 0;
    uint16_t extendedProtocolId = 0;
    bool success = true;

    uint32_t callId = 0;
    uint32_t methodId = 0;
    Error error = Error::NONE;
};

} // namespace nex::rmc


#endif //SPLATOON_SERVER_RMC_RESPONSE_HPP
