#ifndef SPLATOON_SERVER_NINTENDOPRESENCEV2_HPP
#define SPLATOON_SERVER_NINTENDOPRESENCEV2_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"
#include "gameKey.hpp"
#include "../common/string.hpp"
#include "../common/buffer.hpp"

namespace nex::rmc {

    class NintendoPresenceV2 : public Data {
    public:
        explicit NintendoPresenceV2(uint8_t minorVersion) : Data(minorVersion), gameKey(minorVersion) {};
        NintendoPresenceV2(const NintendoPresenceV2& other) = default;
        NintendoPresenceV2(NintendoPresenceV2&& other) noexcept = default;
        ~NintendoPresenceV2() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto changedFlagsData = changedFlags.encode();
            data.insert(data.end(), changedFlagsData.begin(), changedFlagsData.end());
            auto onlineData = online.encode();
            data.insert(data.end(), onlineData.begin(), onlineData.end());
            auto gameKeyData = gameKey.encode();
            data.insert(data.end(), gameKeyData.begin(), gameKeyData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto messageData = message.encode();
            data.insert(data.end(), messageData.begin(), messageData.end());
            auto unk2Data = unk2.encode();
            data.insert(data.end(), unk2Data.begin(), unk2Data.end());
            auto unk3Data = unk3.encode();
            data.insert(data.end(), unk3Data.begin(), unk3Data.end());
            auto gameServerIdData = gameServerId.encode();
            data.insert(data.end(), gameServerIdData.begin(), gameServerIdData.end());
            auto unk4Data = unk4.encode();
            data.insert(data.end(), unk4Data.begin(), unk4Data.end());
            auto pidData = pid.encode();
            data.insert(data.end(), pidData.begin(), pidData.end());
            auto gatheringIdData = gatheringId.encode();
            data.insert(data.end(), gatheringIdData.begin(), gatheringIdData.end());
            auto appDataData = appData.encode();
            data.insert(data.end(), appDataData.begin(), appDataData.end());
            auto unk5Data = unk5.encode();
            data.insert(data.end(), unk5Data.begin(), unk5Data.end());
            auto unk6Data = unk6.encode();
            data.insert(data.end(), unk6Data.begin(), unk6Data.end());
            auto unk7Data = unk7.encode();
            data.insert(data.end(), unk7Data.begin(), unk7Data.end());

            auto header = encodeHeader(NINTENDO_PRESENCE_V2_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, NINTENDO_PRESENCE_V2_VERSION);
            size += changedFlags.decode(data.subspan(size));
            size += online.decode(data.subspan(size));
            size += gameKey.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));
            size += message.decode(data.subspan(size));
            size += unk2.decode(data.subspan(size));
            size += unk3.decode(data.subspan(size));
            size += gameServerId.decode(data.subspan(size));
            size += unk4.decode(data.subspan(size));
            size += pid.decode(data.subspan(size));
            size += gatheringId.decode(data.subspan(size));
            size += appData.decode(data.subspan(size));
            size += unk5.decode(data.subspan(size));
            size += unk6.decode(data.subspan(size));
            size += unk7.decode(data.subspan(size));

            return size;
        }

        NintendoPresenceV2& operator=(const NintendoPresenceV2& other) = default;
        NintendoPresenceV2& operator=(NintendoPresenceV2&& other) noexcept = default;

        UInt32 changedFlags; // https://github.com/kinnay/NintendoClients/wiki/Friends-Protocol-(Wii-U)#changed-flags
        Bool online;
        GameKey gameKey;
        UInt8 unk1;
        String message;
        UInt32 unk2;
        UInt8 unk3;
        UInt32 gameServerId;
        UInt32 unk4;
        PID pid;
        UInt32 gatheringId;
        Buffer appData;
        UInt8 unk5;
        UInt8 unk6;
        UInt8 unk7;

    private:
        const inline static uint8_t NINTENDO_PRESENCE_V2_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_NINTENDOPRESENCEV2_HPP
