#ifndef SPLATOON_SERVER_FRIENDREQUESTMSG_HPP
#define SPLATOON_SERVER_FRIENDREQUESTMSG_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"
#include "../common/string.hpp"
#include "gameKey.hpp"
#include "../common/datetime.hpp"

namespace nex::rmc {

    class FriendRequestMsg : public Data {
    public:
        explicit FriendRequestMsg(uint8_t minorVersion) : Data(minorVersion), gameKey(minorVersion) {};

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto idData = id.encode();
            data.insert(data.end(), idData.begin(), idData.end());
            auto isReceivedData = isReceived.encode();
            data.insert(data.end(), isReceivedData.begin(), isReceivedData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto messageData = message.encode();
            data.insert(data.end(), messageData.begin(), messageData.end());
            auto unk2Data = unk2.encode();
            data.insert(data.end(), unk2Data.begin(), unk2Data.end());
            auto unk3Data = unk3.encode();
            data.insert(data.end(), unk3Data.begin(), unk3Data.end());
            auto gameKeyData = gameKey.encode();
            data.insert(data.end(), gameKeyData.begin(), gameKeyData.end());
            auto unk4Data = unk4.encode();
            data.insert(data.end(), unk4Data.begin(), unk4Data.end());
            auto expiresOnData = expiresOn.encode();
            data.insert(data.end(), expiresOnData.begin(), expiresOnData.end());

            auto header = encodeHeader(FRIEND_REQUEST_MSG_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, FRIEND_REQUEST_MSG_VERSION);
            size += id.decode(data.subspan(size));
            size += isReceived.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));
            size += message.decode(data.subspan(size));
            size += unk2.decode(data.subspan(size));
            size += unk3.decode(data.subspan(size));
            size += gameKey.decode(data.subspan(size));
            size += unk4.decode(data.subspan(size));
            size += expiresOn.decode(data.subspan(size));

            return size;
        }

        UInt64 id;
        UInt8 isReceived; // Should this be a bool?, the docs say it's a byte
        UInt8 unk1;
        String message;
        UInt8 unk2;
        String unk3;
        GameKey gameKey;
        Datetime unk4;
        Datetime expiresOn;

    private:
        const inline static uint8_t FRIEND_REQUEST_MSG_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDREQUESTMSG_HPP
