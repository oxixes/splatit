#ifndef SPLATOON_SERVER_GAMEKEY_HPP
#define SPLATOON_SERVER_GAMEKEY_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"

namespace nex::rmc {

    class GameKey : public Data {
    public:
        explicit GameKey(uint8_t minorVersion) : Data(minorVersion) {};

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto titleIdData = titleId.encode();
            data.insert(data.end(), titleIdData.begin(), titleIdData.end());
            auto titleVersionData = titleVersion.encode();
            data.insert(data.end(), titleVersionData.begin(), titleVersionData.end());

            auto header = encodeHeader(GAME_KEY_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, GAME_KEY_VERSION);
            size += titleId.decode(data.subspan(size));
            size += titleVersion.decode(data.subspan(size));

            return size;
        }

        UInt64 titleId;
        UInt16 titleVersion;

    private:
        const inline static uint8_t GAME_KEY_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_GAMEKEY_HPP
