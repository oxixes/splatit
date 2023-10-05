#ifndef SPLATOON_SERVER_NINTENDONOTIFICATIONEVENT_HPP
#define SPLATOON_SERVER_NINTENDONOTIFICATIONEVENT_HPP

#include "../common/structure.hpp"
#include "../common/ints.hpp"
#include "../common/anyDataHolder.hpp"

namespace nex::rmc {

    enum class NintendoNotificationType {
        NONE = 0,
        WENT_OFFLINE = 10,
        MII_CHANGED = 21,
        PREFERENCES_UPDATED = 23,
        PRESENCE_UPDATED = 24,
        FRIEND_REMOVED = 26,
        FRIEND_REQUEST_RECEIVED = 27,
        FRIEND_REQUEST_ACCEPTED = 30,
        COMMENT_CHANGED = 33
    };

    class NintendoNotificationEvent : public Structure {
    public:
        explicit NintendoNotificationEvent(uint8_t minorVersion) : Structure(minorVersion) {};

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto typeData = UInt32(0, static_cast<uint32_t>(type)).encode();
            data.insert(data.end(), typeData.begin(), typeData.end());
            auto senderData = sender.encode();
            data.insert(data.end(), senderData.begin(), senderData.end());
            auto eventDataData = eventData.encode();
            data.insert(data.end(), eventDataData.begin(), eventDataData.end());

            auto result = encodeHeader(NINTENDO_NOTIFICATION_EVENT_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;
            size += decodeHeader(data, NINTENDO_NOTIFICATION_EVENT_VERSION);
            UInt32 typeData;
            size += typeData.decode(data.subspan(size));
            type = static_cast<NintendoNotificationType>((uint32_t) typeData);
            size += sender.decode(data.subspan(size));
            size += eventData.decode(data.subspan(size));

            return size;
        }

        static uint32_t getMethodForType(NintendoNotificationType type) {
            switch (type) {
                case NintendoNotificationType::MII_CHANGED:
                case NintendoNotificationType::PRESENCE_UPDATED:
                case NintendoNotificationType::FRIEND_REQUEST_RECEIVED:
                    return 2;
                case NintendoNotificationType::WENT_OFFLINE:
                case NintendoNotificationType::PREFERENCES_UPDATED:
                case NintendoNotificationType::FRIEND_REMOVED:
                case NintendoNotificationType::FRIEND_REQUEST_ACCEPTED:
                case NintendoNotificationType::COMMENT_CHANGED:
                default:
                    return 1;
            }
        }

        NintendoNotificationType type = NintendoNotificationType::NONE;
        PID sender;
        AnyDataHolder eventData;

    private:
        const inline static uint8_t NINTENDO_NOTIFICATION_EVENT_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_NINTENDONOTIFICATIONEVENT_HPP
