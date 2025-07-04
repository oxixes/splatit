#ifndef SPLATOON_SERVER_MIIV2_HPP
#define SPLATOON_SERVER_MIIV2_HPP

#include "../common/string.hpp"
#include "../common/ints.hpp"
#include "../common/buffer.hpp"
#include "../common/datetime.hpp"
#include "../common/data.hpp"

namespace nex::rmc {

    class MiiV2 : public Data {
    public:
        explicit MiiV2(uint8_t minorVersion) : Data(minorVersion) {};
        MiiV2(const MiiV2& other) = default;
        MiiV2(MiiV2&& other) noexcept = default;
        ~MiiV2() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;
            auto nameData = name.encode();
            data.insert(data.end(), nameData.begin(), nameData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto unk2Data = unk2.encode();
            data.insert(data.end(), unk2Data.begin(), unk2Data.end());
            auto miiDataData = miiData.encode();
            data.insert(data.end(), miiDataData.begin(), miiDataData.end());
            auto unk3Data = unk3.encode();
            data.insert(data.end(), unk3Data.begin(), unk3Data.end());

            auto header = encodeHeader(MIIV2_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, MIIV2_VERSION);
            size += name.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));
            size += unk2.decode(data.subspan(size));
            size += miiData.decode(data.subspan(size));
            size += unk3.decode(data.subspan(size));

            return size;
        }

        MiiV2& operator=(const MiiV2& other) = default;
        MiiV2& operator=(MiiV2&& other) noexcept = default;

        String name;
        UInt8 unk1;
        UInt8 unk2;
        Buffer miiData; // https://wut.devkitpro.org/group__nn__ffl__miidata.html#structFFLStoreData
        Datetime unk3;

    private:
        const inline static uint8_t MIIV2_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_MIIV2_HPP
