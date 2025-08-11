#ifndef SPLATOON_SERVER_NINTENDOCREATEACCOUNTDATA_HPP
#define SPLATOON_SERVER_NINTENDOCREATEACCOUNTDATA_HPP

#include "NNAInfo.hpp"
#include "../common/structure.hpp"

namespace nex::rmc {

    class NintendoCreateAccountData : public Structure {
    public:
        explicit NintendoCreateAccountData(uint8_t minorVersion) : Structure(minorVersion), nnaInfo(minorVersion) {}
        NintendoCreateAccountData(const NintendoCreateAccountData& other) = default;
        NintendoCreateAccountData(NintendoCreateAccountData&& other) noexcept = default;
        ~NintendoCreateAccountData() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto nnaInfoData = nnaInfo.encode();
            data.insert(data.end(), nnaInfoData.begin(), nnaInfoData.end());
            auto nexTokenData = nexToken.encode();
            data.insert(data.end(), nexTokenData.begin(), nexTokenData.end());
            auto birthdateData = birthdate.encode();
            data.insert(data.end(), birthdateData.begin(), birthdateData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());

            auto header = encodeHeader(NINTENDO_CREATE_ACCOUNT_DATA_VERSION, data.size());
            data.insert(data.begin(), header.begin(), header.end());

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data.subspan(size), NINTENDO_CREATE_ACCOUNT_DATA_VERSION);
            size += nnaInfo.decode(data.subspan(size));
            size += nexToken.decode(data.subspan(size));
            size += birthdate.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));

            return size;
        }

        NintendoCreateAccountData& operator=(const NintendoCreateAccountData& other) = default;
        NintendoCreateAccountData& operator=(NintendoCreateAccountData&& other) noexcept = default;

        NNAInfo nnaInfo;
        String nexToken;
        Datetime birthdate;
        UInt64 unk1;

    private:
        const inline static uint8_t NINTENDO_CREATE_ACCOUNT_DATA_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_NINTENDOCREATEACCOUNTDATA_HPP
