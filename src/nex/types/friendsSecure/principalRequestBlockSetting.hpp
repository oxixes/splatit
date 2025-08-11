#ifndef SPLATOON_SERVER_PRINCIPALREQUESTBLOCKSETTING_HPP
#define SPLATOON_SERVER_PRINCIPALREQUESTBLOCKSETTING_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"

namespace nex::rmc {

    class PrincipalRequestBlockSetting : public Data {
    public:
        explicit PrincipalRequestBlockSetting(uint8_t minorVersion) : Data(minorVersion) {};
        PrincipalRequestBlockSetting(const PrincipalRequestBlockSetting& other) = default;
        PrincipalRequestBlockSetting(PrincipalRequestBlockSetting&& other) noexcept = default;
        ~PrincipalRequestBlockSetting() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto pidData = pid.encode();
            data.insert(data.end(), pidData.begin(), pidData.end());
            auto blockedData = blocked.encode();
            data.insert(data.end(), blockedData.begin(), blockedData.end());

            auto header = encodeHeader(PRINCIPAL_REQUEST_BLOCK_SETTING_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, PRINCIPAL_REQUEST_BLOCK_SETTING_VERSION);
            size += pid.decode(data.subspan(size));
            size += blocked.decode(data.subspan(size));

            return size;
        }

        PrincipalRequestBlockSetting& operator=(const PrincipalRequestBlockSetting& other) = default;
        PrincipalRequestBlockSetting& operator=(PrincipalRequestBlockSetting&& other) noexcept = default;

        PID pid;
        Bool blocked;

    private:
        const inline static uint8_t PRINCIPAL_REQUEST_BLOCK_SETTING_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_PRINCIPALREQUESTBLOCKSETTING_HPP
