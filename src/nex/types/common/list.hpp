#ifndef SPLATOON_SERVER_LIST_HPP
#define SPLATOON_SERVER_LIST_HPP

#include "../../rmc/types.hpp"
#include "ints.hpp"

namespace nex::rmc {

    template<typename T> requires std::is_base_of_v<Type, T>
    class List : public Type {
    public:
        explicit List(uint8_t minorVersion = 0, const std::vector<T>& data = {}) : Type(minorVersion), data(data) {}
        explicit List(const std::vector<T>& data = {}) : Type(0), data(data) {}

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> encoded;
            encoded.reserve(sizeof(uint32_t));

            auto length = Int<uint32_t>((uint32_t) data.size()).encode();
            encoded.insert(encoded.end(), length.begin(), length.end());
            for (const auto& item : data) {
                auto encodedItem = item.encode();
                encoded.insert(encoded.end(), encodedItem.begin(), encodedItem.end());
            }

            return encoded;
        }

        size_t decode(std::span<const uint8_t> listData) override {
            if (listData.size() < sizeof(uint32_t)) throw MalformedException("Not enough data to decode List");

            auto length = Int<uint32_t>();
            length.decode(listData);
            listData = listData.subspan(sizeof(uint32_t));

            size_t dataSize = 0;

            data.reserve(length);
            for (int i = 0; i < length; i++) {
                auto item = T(minorVersion);
                size_t decodedLength = item.decode(listData);
                data.push_back(std::move(item));

                listData = listData.subspan(decodedLength);
                dataSize += decodedLength;
            }

            return sizeof(uint32_t) + dataSize;
        }

        explicit operator std::vector<T>() const { return data; }
        explicit operator std::vector<T>&() { return data; }
        explicit operator const std::vector<T>&() const { return data; }
        T& operator [](size_t index) { return data[index]; }

    private:
        std::vector<T> data;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_LIST_HPP
