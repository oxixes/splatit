#ifndef SPLATOON_SERVER_ANYDATAHOLDER_HPP
#define SPLATOON_SERVER_ANYDATAHOLDER_HPP

#include <any>

#include "../../../util/util.hpp"
#include "../../rmc/types.hpp"
#include "string.hpp"

namespace nex::rmc {

    class AnyDataHolder : public Type {
    public:
        explicit AnyDataHolder(uint8_t minorVersion = 0) : Type(minorVersion) {};
        ~AnyDataHolder() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;
            String typeStr(type);
            auto typeData = typeStr.encode();
            data.insert(data.end(), typeData.begin(), typeData.end());

            auto dataLength = (uint32_t) (objData.size() + sizeof(uint32_t));
            util::getu32Little(dataLength);
            data.insert(data.end(), (uint8_t*) &dataLength, (uint8_t*) &dataLength + sizeof(uint32_t));

            auto length = (uint32_t) objData.size();
            util::getu32Little(length);
            data.insert(data.end(), (uint8_t*) &length, (uint8_t*) &length + sizeof(uint32_t));

            data.insert(data.end(), objData.begin(), objData.end());

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            auto typeStr = String(minorVersion);
            size_t typeLength = typeStr.decode(data);
            type = typeStr;

            if (data.size() < typeLength + sizeof(uint32_t))
                throw MalformedException("Not enough data to decode AnyDataHolder");

            uint32_t dataLength;
            memcpy(&dataLength, &data[typeLength], sizeof(uint32_t));
            util::getu32Little(dataLength);

            if (data.size() < typeLength + dataLength + sizeof(uint32_t))
                throw MalformedException("Invalid AnyDataHolder length");

            uint32_t length;
            memcpy(&length, &data[typeLength + sizeof(uint32_t)], sizeof(uint32_t));
            util::getu32Little(length);

            if (dataLength != length + sizeof(uint32_t))
                throw MalformedException("Invalid AnyDataHolder data length");

            objData.resize(length);
            memcpy(objData.data(), &data[typeLength + sizeof(uint32_t) + sizeof(uint32_t)], length);

            return typeLength + dataLength + sizeof(uint32_t);
        }

        template <typename T> requires std::is_base_of_v<Type, T>
        T get() const {
            T obj(minorVersion);
            obj.decode(objData);
            return obj;
        }

        template <typename T> requires std::is_base_of_v<Type, T>
        void set(const T& obj, const std::string& typeName) {
            objData = obj.encode();
            type = typeName;
        }

        [[nodiscard]] std::string getType() const {
            return type;
        }

    private:
        std::string type;
        std::vector<uint8_t> objData;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_ANYDATAHOLDER_HPP
