#ifndef SPLATOON_SERVER_MAP_HPP
#define SPLATOON_SERVER_MAP_HPP

#include <map>

#include "../../rmc/types.hpp"
#include "ints.hpp"

namespace nex::rmc {

    template<typename K, typename T> requires (std::is_base_of_v<Type, K> && std::is_base_of_v<Type, T>)
    class Map : public Type {
    public:
        explicit Map(uint8_t minorVersion = 0, const std::map<K, T>& data = {}) : Type(minorVersion), data(data) {}
        explicit Map(const std::map<K, T>& data = {}) : Type(0), data(data) {}

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> encoded;
            encoded.reserve(sizeof(uint32_t));

            auto length = Int<uint32_t>((uint32_t) data.size()).encode();
            encoded.insert(encoded.end(), length.begin(), length.end());
            for (const auto& [key, value] : data) {
                auto encodedKey = key->encode();
                encoded.insert(encoded.end(), encodedKey.begin(), encodedKey.end());

                auto encodedValue = value->encode();
                encoded.insert(encoded.end(), encodedValue.begin(), encodedValue.end());
            }

            return encoded;
        }

        size_t decode(std::span<const uint8_t> mapData) override {
            if (mapData.size() < sizeof(uint32_t)) throw MalformedException("Not enough data to decode Map");

            auto length = Int<uint32_t>();
            length.decode(mapData);

            size_t dataSize = 0;

            data.reserve(length);
            for (int i = 0; i < length; i++) {
                auto key = K(minorVersion);
                auto decodedKeyLength = key.decode(mapData);

                auto value = T(minorVersion);
                auto decodedValueLength = value.decode(mapData.subspan(decodedKeyLength));

                data.insert({std::move(key), std::move(value)});

                mapData = mapData.subspan(decodedKeyLength + decodedValueLength);
                dataSize += decodedKeyLength + decodedValueLength;
            }

            return sizeof(uint32_t) + dataSize;
        }

    private:
        std::map<K, T> data;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_MAP_HPP
