#ifndef SPLATOON_SERVER_FRIENDREQUEST_HPP
#define SPLATOON_SERVER_FRIENDREQUEST_HPP

#include "../common/data.hpp"
#include "principalBasicInfo.hpp"
#include "friendRequestMsg.hpp"

namespace nex::rmc {

    class FriendRequest : public Data {
    public:
        explicit FriendRequest(uint8_t minorVersion) : Data(minorVersion), principalBasicInfo(minorVersion),
                                                       friendRequestMsg(minorVersion) {};

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto principalBasicInfoData = principalBasicInfo.encode();
            data.insert(data.end(), principalBasicInfoData.begin(), principalBasicInfoData.end());
            auto friendRequestMsgData = friendRequestMsg.encode();
            data.insert(data.end(), friendRequestMsgData.begin(), friendRequestMsgData.end());
            auto sentOnData = sentOn.encode();
            data.insert(data.end(), sentOnData.begin(), sentOnData.end());

            auto header = encodeHeader(FRIEND_REQUEST_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, FRIEND_REQUEST_VERSION);
            size += principalBasicInfo.decode(data.subspan(size));
            size += friendRequestMsg.decode(data.subspan(size));
            size += sentOn.decode(data.subspan(size));

            return size;
        }

        PrincipalBasicInfo principalBasicInfo;
        FriendRequestMsg friendRequestMsg;
        Datetime sentOn;

    private:
        const inline static uint8_t FRIEND_REQUEST_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDREQUEST_HPP
