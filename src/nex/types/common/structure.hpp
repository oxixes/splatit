#ifndef SPLATOON_SERVER_STRUCTURE_HPP
#define SPLATOON_SERVER_STRUCTURE_HPP

#include "../../../util/util.hpp"
#include "../../rmc/types.hpp"
#include "../../../exceptions.hpp"

namespace nex::rmc {

    class Structure : public Type {
    public:
        ~Structure() override = default;

    protected:
        explicit Structure(uint8_t minorVersion) : Type(minorVersion) {};

        [[nodiscard]] std::vector<uint8_t> encodeHeader(uint8_t version, uint32_t length) const {
            if (minorVersion < 3) return {};

            std::vector<uint8_t> data(sizeof(uint8_t) + sizeof(uint32_t));
            data[0] = version;
            util::getu32Little(length);
            memcpy(&data[1], &length, sizeof(uint32_t));
            return data;
        }

        size_t decodeHeader(std::span<const uint8_t> data, uint8_t expectedVersion) {
            if (minorVersion < 3) return 0;

            if (data.size() < sizeof(uint8_t) + sizeof(uint32_t)) throw MalformedException("Not enough data to decode Structure header");

            uint8_t version = data[0];
            if (version != expectedVersion) throw MalformedException("Invalid Structure version: expected "
                    + std::to_string(expectedVersion) + ", got " + std::to_string(version));

            uint32_t length;
            memcpy(&length, &data[1], sizeof(uint32_t));
            util::getu32Little(length);

            if (data.size() < length + sizeof(uint8_t) + sizeof(uint32_t)) throw MalformedException("Invalid Structure length");

            return sizeof(uint8_t) + sizeof(uint32_t);
        }
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_STRUCTURE_HPP
