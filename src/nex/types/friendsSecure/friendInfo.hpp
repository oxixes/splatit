#ifndef SPLATOON_SERVER_FRIENDINFO_HPP
#define SPLATOON_SERVER_FRIENDINFO_HPP

#include "../common/data.hpp"
#include "NNAInfo.hpp"
#include "nintendoPresenceV2.hpp"
#include "comment.hpp"

namespace nex::rmc {

    class FriendInfo : public Data {
    public:
        explicit FriendInfo(uint8_t minorVersion) : Data(minorVersion), nnaInfo(minorVersion), presence(minorVersion),
                                                    comment(minorVersion) {};
        FriendInfo(const FriendInfo& other) = default;
        FriendInfo(FriendInfo&& other) noexcept = default;
        ~FriendInfo() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto nnaInfoData = nnaInfo.encode();
            data.insert(data.end(), nnaInfoData.begin(), nnaInfoData.end());
            auto presenceData = presence.encode();
            data.insert(data.end(), presenceData.begin(), presenceData.end());
            auto commentData = comment.encode();
            data.insert(data.end(), commentData.begin(), commentData.end());
            auto becameFriendsData = becameFriends.encode();
            data.insert(data.end(), becameFriendsData.begin(), becameFriendsData.end());
            auto lastOnlineData = lastOnline.encode();
            data.insert(data.end(), lastOnlineData.begin(), lastOnlineData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());

            auto header = encodeHeader(FRIEND_INFO_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, FRIEND_INFO_VERSION);
            size += nnaInfo.decode(data.subspan(size));
            size += presence.decode(data.subspan(size));
            size += comment.decode(data.subspan(size));
            size += becameFriends.decode(data.subspan(size));
            size += lastOnline.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));

            return size;
        }

        FriendInfo& operator=(const FriendInfo& other) = default;
        FriendInfo& operator=(FriendInfo&& other) noexcept = default;

        NNAInfo nnaInfo;
        NintendoPresenceV2 presence;
        Comment comment;
        Datetime becameFriends;
        Datetime lastOnline;
        UInt64 unk1;

    private:
        const inline static uint8_t FRIEND_INFO_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDINFO_HPP
