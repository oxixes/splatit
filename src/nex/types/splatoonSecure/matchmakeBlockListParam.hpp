#ifndef SPLATOON_SERVER_MATCHMAKEBLOCKLISTPARAM_HPP
#define SPLATOON_SERVER_MATCHMAKEBLOCKLISTPARAM_HPP

#include "../common/structure.hpp"
#include "../common/ints.hpp"

namespace nex::rmc {

    class MatchmakeBlockListParam : public Structure {
    public:
        explicit MatchmakeBlockListParam(uint8_t minorVersion) : Structure(minorVersion) {};
        MatchmakeBlockListParam(const MatchmakeBlockListParam& other) = default;
        MatchmakeBlockListParam(MatchmakeBlockListParam&& other) noexcept = default;
        ~MatchmakeBlockListParam() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto optionFlagData = optionFlag.encode();
            data.insert(data.end(), optionFlagData.begin(), optionFlagData.end());

            auto result = encodeHeader(MATCHMAKE_BLOCK_LIST_PARAM_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, MATCHMAKE_BLOCK_LIST_PARAM_VERSION);
            size += optionFlag.decode(data.subspan(size));

            return size;
        }

        MatchmakeBlockListParam& operator=(const MatchmakeBlockListParam& other) = default;
        MatchmakeBlockListParam& operator=(MatchmakeBlockListParam&& other) noexcept = default;

        UInt32 optionFlag;

    private:
        const inline static uint8_t MATCHMAKE_BLOCK_LIST_PARAM_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_MATCHMAKEBLOCKLISTPARAM_HPP
