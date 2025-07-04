#ifndef SPLATOON_SERVER_BLACKLISTEDPRINCIPAL_HPP
#define SPLATOON_SERVER_BLACKLISTEDPRINCIPAL_HPP

#include "../common/data.hpp"
#include "principalBasicInfo.hpp"
#include "gameKey.hpp"
#include "../common/datetime.hpp"

namespace nex::rmc {

    class BlacklistedPrincipal : public Data {
    public:
        explicit BlacklistedPrincipal(uint8_t minorVersion) : Data(minorVersion), principalBasicInfo(minorVersion),
                                                              gameKey(minorVersion) {};
        BlacklistedPrincipal(const BlacklistedPrincipal& other) = default;
        BlacklistedPrincipal(BlacklistedPrincipal&& other) noexcept = default;
        ~BlacklistedPrincipal() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto principalBasicInfoData = principalBasicInfo.encode();
            data.insert(data.end(), principalBasicInfoData.begin(), principalBasicInfoData.end());
            auto unk1Data = gameKey.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto unk2Data = blacklistedSince.encode();
            data.insert(data.end(), unk2Data.begin(), unk2Data.end());

            auto header = encodeHeader(BLACKLISTED_PRINCIPAL_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, BLACKLISTED_PRINCIPAL_VERSION);
            size += principalBasicInfo.decode(data.subspan(size));
            size += gameKey.decode(data.subspan(size));
            size += blacklistedSince.decode(data.subspan(size));

            return size;
        }

        BlacklistedPrincipal& operator=(const BlacklistedPrincipal& other) = default;
        BlacklistedPrincipal& operator=(BlacklistedPrincipal&& other) noexcept = default;

        PrincipalBasicInfo principalBasicInfo;
        GameKey gameKey;
        Datetime blacklistedSince;

    private:
        const inline static uint8_t BLACKLISTED_PRINCIPAL_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_BLACKLISTEDPRINCIPAL_HPP
