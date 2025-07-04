#ifndef SPLATOON_SERVER_COMMENT_HPP
#define SPLATOON_SERVER_COMMENT_HPP

#include "../common/data.hpp"
#include "../common/ints.hpp"
#include "../common/string.hpp"
#include "../common/datetime.hpp"

namespace nex::rmc {

    class Comment : public Data {
    public:
        explicit Comment(uint8_t minorVersion) : Data(minorVersion) {};
        Comment(const Comment& other) = default;
        Comment(Comment&& other) noexcept = default;
        ~Comment() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto messageData = message.encode();
            data.insert(data.end(), messageData.begin(), messageData.end());
            auto lastModifiedData = lastModified.encode();
            data.insert(data.end(), lastModifiedData.begin(), lastModifiedData.end());

            auto header = encodeHeader(COMMENT_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, COMMENT_VERSION);
            size += unk1.decode(data.subspan(size));
            size += message.decode(data.subspan(size));
            size += lastModified.decode(data.subspan(size));

            return size;
        }

        Comment& operator=(const Comment& other) = default;
        Comment& operator=(Comment&& other) noexcept = default;

        UInt8 unk1;
        String message;
        Datetime lastModified;

    private:
        const inline static uint8_t COMMENT_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_COMMENT_HPP
