#ifndef SPLATOON_SERVER_MATCHMAKEPARAM_HPP
#define SPLATOON_SERVER_MATCHMAKEPARAM_HPP

#include "../common/structure.hpp"
#include "../common/variant.hpp"
#include "../common/string.hpp"
#include "../common/map.hpp"

namespace nex::rmc {

    class MatchmakeParam : public Structure {
    public:
        explicit MatchmakeParam(uint8_t minorVersion) : Structure(minorVersion), params(minorVersion) {};
        ~MatchmakeParam() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto paramsData = params.encode();
            data.insert(data.end(), paramsData.begin(), paramsData.end());

            auto result = encodeHeader(MATCHMAKE_PARAM_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, MATCHMAKE_PARAM_VERSION);
            size += params.decode(data.subspan(size));

            return size;
        }

        Map<String, Variant> params;

    private:
        const inline static uint8_t MATCHMAKE_PARAM_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_MATCHMAKEPARAM_HPP
