#ifndef SPLATOON_SERVER_RESULT_HPP
#define SPLATOON_SERVER_RESULT_HPP

#include "../../rmc/types.hpp"
#include "../../rmc/errors.hpp"
#include "../../../util/util.hpp"
#include "../../../exceptions.hpp"

namespace nex::rmc {

    class Result : public Type {
    public:
        explicit Result(uint8_t minorVersion = 0) : Type(minorVersion) {}
        Result(const Result& other) = default;
        Result(Result&& other) noexcept = default;
        ~Result() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data(sizeof(uint32_t));
            uint32_t value = static_cast<uint32_t>(code) | (success ? 0 : 0x80000000);
            util::getu32Little(value);

            memcpy(data.data(), &value, sizeof(uint32_t));
            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            if (data.size() < sizeof(uint32_t)) throw MalformedException("Not enough data to decode Result");

            uint32_t value;
            memcpy(&value, data.data(), sizeof(uint32_t));
            util::getu32Little(value);

            code = static_cast<Error>(value & 0x7FFFFFFF);
            success = !(value & 0x80000000);

            return sizeof(uint32_t);
        }

        Result& operator=(const Result& other) = default;
        Result& operator=(Result&& other) noexcept = default;

        Error code = Error::NONE;
        bool success = true;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_RESULT_HPP
