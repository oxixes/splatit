#ifndef SPLATOON_SERVER_NNAINFO_HPP
#define SPLATOON_SERVER_NNAINFO_HPP

#include "../common/data.hpp"
#include "principalBasicInfo.hpp"

namespace nex::rmc {

    class NNAInfo : public Data {
    public:
        explicit NNAInfo(uint8_t minorVersion) : Data(minorVersion), info(minorVersion) {};
        NNAInfo(const NNAInfo& other) = default;
        NNAInfo(NNAInfo&& other) noexcept = default;
        ~NNAInfo() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto infoData = info.encode();
            data.insert(data.end(), infoData.begin(), infoData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto unk2Data = unk2.encode();
            data.insert(data.end(), unk2Data.begin(), unk2Data.end());

            auto header = encodeHeader(NNA_INFO_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, NNA_INFO_VERSION);
            size += info.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));
            size += unk2.decode(data.subspan(size));

            return size;
        }

        NNAInfo& operator=(const NNAInfo& other) = default;
        NNAInfo& operator=(NNAInfo&& other) noexcept = default;

        PrincipalBasicInfo info;
        UInt8 unk1;
        UInt8 unk2;

    private:
        const inline static uint8_t NNA_INFO_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_NNAINFO_HPP
