#ifndef SPLATOON_SERVER_DATA_HPP
#define SPLATOON_SERVER_DATA_HPP

#include "structure.hpp"

namespace nex::rmc {

    class Data : public Structure {
    public:
        explicit Data(uint8_t minorVersion) : Structure(minorVersion) {};
        ~Data() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            return encodeHeader(0, 0);
        }

        size_t decode(std::span<const uint8_t> data) override {
            return decodeHeader(data, 0);
        }
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_DATA_HPP
