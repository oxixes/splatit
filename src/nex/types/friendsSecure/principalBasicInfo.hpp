#ifndef SPLATOON_SERVER_PRINCIPALBASICINFO_HPP
#define SPLATOON_SERVER_PRINCIPALBASICINFO_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"
#include "../common/string.hpp"
#include "miiV2.hpp"

namespace nex::rmc {

    class PrincipalBasicInfo : public Data {
    public:
        explicit PrincipalBasicInfo(uint8_t minorVersion) : Data(minorVersion), mii(minorVersion) {};
        PrincipalBasicInfo(const PrincipalBasicInfo& other) = default;
        PrincipalBasicInfo(PrincipalBasicInfo&& other) noexcept = default;
        ~PrincipalBasicInfo() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto pidData = pid.encode();
            data.insert(data.end(), pidData.begin(), pidData.end());
            auto NNIDData = NNID.encode();
            data.insert(data.end(), NNIDData.begin(), NNIDData.end());
            auto miiData = mii.encode();
            data.insert(data.end(), miiData.begin(), miiData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());

            auto header = encodeHeader(PRINCIPAL_BASIC_INFO_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, PRINCIPAL_BASIC_INFO_VERSION);
            size += pid.decode(data.subspan(size));
            size += NNID.decode(data.subspan(size));
            size += mii.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));

            return size;
        }

        PrincipalBasicInfo& operator=(const PrincipalBasicInfo& other) = default;
        PrincipalBasicInfo& operator=(PrincipalBasicInfo&& other) noexcept = default;

        PID pid;
        String NNID;
        MiiV2 mii;
        UInt8 unk1;

    private:
        const inline static uint8_t PRINCIPAL_BASIC_INFO_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_PRINCIPALBASICINFO_HPP
