#ifndef SPLATOON_SERVER_NINTENDONOTIFICATIONEVENTGENERAL_HPP
#define SPLATOON_SERVER_NINTENDONOTIFICATIONEVENTGENERAL_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"
#include "../common/string.hpp"

namespace nex::rmc {

    class NintendoNotificationEventGeneral : public Data {
    public:
        explicit NintendoNotificationEventGeneral(uint8_t minorVersion) : Data(minorVersion) {};
        NintendoNotificationEventGeneral(const NintendoNotificationEventGeneral& other) = default;
        NintendoNotificationEventGeneral(NintendoNotificationEventGeneral&& other) noexcept = default;
        ~NintendoNotificationEventGeneral() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto u32_paramData = u32_param.encode();
            data.insert(data.end(), u32_paramData.begin(), u32_paramData.end());
            auto u64_param1Data = u64_param1.encode();
            data.insert(data.end(), u64_param1Data.begin(), u64_param1Data.end());
            auto u64_param2Data = u64_param2.encode();
            data.insert(data.end(), u64_param2Data.begin(), u64_param2Data.end());
            auto str_paramData = str_param.encode();
            data.insert(data.end(), str_paramData.begin(), str_paramData.end());

            auto header = encodeHeader(NINTENDO_NOTIFICATION_EVENT_GENERAL_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, NINTENDO_NOTIFICATION_EVENT_GENERAL_VERSION);
            size += u32_param.decode(data.subspan(size));
            size += u64_param1.decode(data.subspan(size));
            size += u64_param2.decode(data.subspan(size));
            size += str_param.decode(data.subspan(size));

            return size + parentSize;
        }

        NintendoNotificationEventGeneral& operator=(const NintendoNotificationEventGeneral& other) = default;
        NintendoNotificationEventGeneral& operator=(NintendoNotificationEventGeneral&& other) noexcept = default;

        UInt32 u32_param;
        UInt64 u64_param1;
        UInt64 u64_param2;
        String str_param;

    private:
        const inline static uint8_t NINTENDO_NOTIFICATION_EVENT_GENERAL_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_NINTENDONOTIFICATIONEVENTGENERAL_HPP
