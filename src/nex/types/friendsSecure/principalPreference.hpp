#ifndef SPLATOON_SERVER_PRINCIPALPREFERENCE_HPP
#define SPLATOON_SERVER_PRINCIPALPREFERENCE_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"

namespace nex::rmc {

    class PrincipalPreference : public Data {
    public:
        explicit PrincipalPreference(uint8_t minorVersion) : Data(minorVersion) {};

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto showOnlineData = showOnline.encode();
            data.insert(data.end(), showOnlineData.begin(), showOnlineData.end());
            auto showPlayingData = showPlaying.encode();
            data.insert(data.end(), showPlayingData.begin(), showPlayingData.end());
            auto blockFriendRequestData = blockFriendRequest.encode();
            data.insert(data.end(), blockFriendRequestData.begin(), blockFriendRequestData.end());

            auto header = encodeHeader(PRINCIPAL_PREFERENCE_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, PRINCIPAL_PREFERENCE_VERSION);
            size += showOnline.decode(data.subspan(size));
            size += showPlaying.decode(data.subspan(size));
            size += blockFriendRequest.decode(data.subspan(size));

            return size;
        }

        Bool showOnline;
        Bool showPlaying;
        Bool blockFriendRequest;

    private:
        const inline static uint8_t PRINCIPAL_PREFERENCE_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_PRINCIPALPREFERENCE_HPP
