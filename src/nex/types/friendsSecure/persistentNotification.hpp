#ifndef SPLATOON_SERVER_PERSISTENTNOTIFICATION_HPP
#define SPLATOON_SERVER_PERSISTENTNOTIFICATION_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"
#include "../common/string.hpp"

namespace nex::rmc {

    class PersistentNotification : public Data {
    public:
        explicit PersistentNotification(uint8_t minorVersion) : Data(minorVersion) {};

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto unk2Data = unk2.encode();
            data.insert(data.end(), unk2Data.begin(), unk2Data.end());
            auto unk3Data = unk3.encode();
            data.insert(data.end(), unk3Data.begin(), unk3Data.end());
            auto unk4Data = unk4.encode();
            data.insert(data.end(), unk4Data.begin(), unk4Data.end());
            auto unk5Data = unk5.encode();
            data.insert(data.end(), unk5Data.begin(), unk5Data.end());

            auto header = encodeHeader(PERSISTENT_NOTIFICATION_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, PERSISTENT_NOTIFICATION_VERSION);
            size += unk1.decode(data.subspan(size));
            size += unk2.decode(data.subspan(size));
            size += unk3.decode(data.subspan(size));
            size += unk4.decode(data.subspan(size));
            size += unk5.decode(data.subspan(size));

            return size;
        }

        UInt64 unk1;
        UInt32 unk2;
        UInt32 unk3;
        UInt32 unk4;
        String unk5;

    private:
        const inline static uint8_t PERSISTENT_NOTIFICATION_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_PERSISTENTNOTIFICATION_HPP
