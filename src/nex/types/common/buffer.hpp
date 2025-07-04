#ifndef SPLATOON_SERVER_BUFFER_HPP
#define SPLATOON_SERVER_BUFFER_HPP

#include "ints.hpp"
#include "../../rmc/types.hpp"

namespace nex::rmc {

    template<typename LengthT>
    class BufferT : public Type {
    public:
        explicit BufferT(uint8_t minorVersion = 0, const std::vector<uint8_t>& data = {}) : Type(minorVersion), data(data) {}
        explicit BufferT(uint8_t minorVersion, std::vector<uint8_t>&& data) : Type(minorVersion), data(std::move(data)) {}
        explicit BufferT(const std::vector<uint8_t>& data) : Type(0), data(data) {}
        explicit BufferT(std::vector<uint8_t>&& data) : Type(0), data(std::move(data)) {}
        BufferT(const BufferT& other) = default;
        BufferT(BufferT&& other) noexcept = default;
        ~BufferT() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> encoded;
            encoded.reserve(sizeof(LengthT) + data.size());

            auto length = Int<LengthT>(0, (LengthT) data.size()).encode();
            encoded.insert(encoded.end(), length.begin(), length.end());
            encoded.insert(encoded.end(), data.begin(), data.end());

            return encoded;
        }

        size_t decode(std::span<const uint8_t> bufData) override {
            if (bufData.size() < sizeof(LengthT)) throw MalformedException("Not enough data to decode Buffer");

            auto length = Int<LengthT>();
            length.decode(bufData);

            if (bufData.size() < sizeof(LengthT) + length) throw MalformedException("Invalid Buffer data length");

            data.resize(length);
            std::copy(bufData.begin() + sizeof(LengthT), bufData.begin() + sizeof(LengthT) + length, data.begin());

            return sizeof(LengthT) + length;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string str = getName() + "(";
            std::stringstream ss;
            for (auto byte : data) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int) byte;
            }
            str += ss.str() + ")";
            return str;
        }

        [[nodiscard]] std::string getName() const override {
            if (sizeof(LengthT) == 4) return "Buffer";
            return "qBuffer";
        }

        BufferT& operator=(const BufferT& other) = default;
        BufferT& operator=(BufferT&& other) noexcept = default;

        std::vector<uint8_t> data;
    };

    typedef BufferT<uint32_t> Buffer;
    typedef BufferT<uint16_t> qBuffer;

} // namespace nex::rmc

#endif //SPLATOON_SERVER_BUFFER_HPP
