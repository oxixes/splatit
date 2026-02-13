#ifndef SPLATOON_SERVER_NOTIFICATIONEVENT_HPP
#define SPLATOON_SERVER_NOTIFICATIONEVENT_HPP

#include "../common/structure.hpp"
#include "../common/ints.hpp"
#include "../common/string.hpp"

namespace nex::rmc {

    enum class NotificationType {
        NONE = 0,
        NEW_PARTICIPANT = 3001,
        PARTICIPATION_CANCELLED = 3002,
        PARTICIPANT_DISCONNECTED = 3007,
        PARTICIPATION_ENDED = 3008,
        OWNERSHIP_CHANGED = 4000,
        GATHERING_UNREGISTERED = 109000,
        HOST_CHANGED = 110000,
        MATCHMAKE_REFEREE_ROUND_STARTED = 116000,
        SYSTEM_PASSWORD_CHANGED = 120000,
        SYSTEM_PASSWORD_CLEARED = 121000,
        SWITCH_GATHERING = 122000
    };

    class NotificationEvent : public Structure {
    public:
        explicit NotificationEvent(uint8_t minorVersion) : Structure(minorVersion) {};
        NotificationEvent(const NotificationEvent& other) = default;
        NotificationEvent(NotificationEvent&& other) noexcept = default;
        ~NotificationEvent() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto srcPidData = srcPid.encode();
            data.insert(data.end(), srcPidData.begin(), srcPidData.end());
            auto typeData = UInt32(0, static_cast<uint32_t>(type)).encode();
            data.insert(data.end(), typeData.begin(), typeData.end());
            auto param1Data = param1.encode();
            data.insert(data.end(), param1Data.begin(), param1Data.end());
            auto param2Data = param2.encode();
            data.insert(data.end(), param2Data.begin(), param2Data.end());
            auto strParamData = strParam.encode();
            data.insert(data.end(), strParamData.begin(), strParamData.end());
            auto param3Data = param3.encode();
            data.insert(data.end(), param3Data.begin(), param3Data.end());

            auto result = encodeHeader(NOTIFICATION_EVENT_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, NOTIFICATION_EVENT_VERSION);
            UInt32 typeInt;
            size += typeInt.decode(data.subspan(size));
            type = static_cast<NotificationType>((uint32_t) typeInt);

            size += param1.decode(data.subspan(size));
            size += param2.decode(data.subspan(size));
            size += strParam.decode(data.subspan(size));
            size += param3.decode(data.subspan(size));

            return size;
        }

        NotificationEvent& operator=(const NotificationEvent& other) = default;
        NotificationEvent& operator=(NotificationEvent&& other) noexcept = default;

        PID srcPid;
        NotificationType type = NotificationType::NONE;
        UInt32 param1;
        UInt32 param2;
        String strParam;
        UInt32 param3;

    private:
        const inline static uint8_t NOTIFICATION_EVENT_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_NOTIFICATIONEVENT_HPP
