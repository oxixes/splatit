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
            for (auto it = data.begin(); it != data.end(); it++) {
                auto encodedKey = it->first.encode();
                encoded.insert(encoded.end(), encodedKey.begin(), encodedKey.end());

                auto encodedValue = it->second.encode();
                encoded.insert(encoded.end(), encodedValue.begin(), encodedValue.end());
            }

            return encoded;
        }

        size_t decode(std::span<const uint8_t> mapData) override {
            if (mapData.size() < sizeof(uint32_t)) throw MalformedException("Not enough data to decode Map");

            auto length = Int<uint32_t>();
            length.decode(mapData);

            size_t dataSize = 0;

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

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string str = getName() + ": {" + (data.empty() ? "}" : "\n");
            if (data.empty()) return str;

            std::string indent((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent(indentation * INDENTATION_SPACES, ' ');
            for (auto& [key, value] : data) {
                str += indent + key.toString(indentation + 1) + ": " + value.toString(indentation + 1) + "\n";
            }
            str += last_indent + "}";
            return str;
        }

        [[nodiscard]] std::string getName() const override {
            std::string name = "Map";
            if (!data.empty()) {
                name += "<" + data.begin()->first.getName() + ", " + data.begin()->second.getName() + ">";
            }
            return name;
        }

        auto begin() { return data.begin(); }
        auto begin() const { return data.begin(); }
        auto end() { return data.end(); }
        auto end() const { return data.end(); }
        auto find(const K& key) { return data.find(key); }
        auto find(const K& key) const { return data.find(key); }
        auto insert(const std::pair<K, T>& pair) { return data.insert(pair); }

        explicit operator std::map<K, T>& () { return data; }
        explicit operator const std::map<K, T>& () const { return data; }
        T& operator[](const K& key) { return data[key]; }

    private:
        std::map<K, T> data;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_MAP_HPP
