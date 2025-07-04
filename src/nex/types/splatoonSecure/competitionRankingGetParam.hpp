#ifndef COMPETITIONRANKINGGETPARAM_HPP
#define COMPETITIONRANKINGGETPARAM_HPP

#include "../common/structure.hpp"
#include "../common/list.hpp"
#include "../common/resultRange.hpp"

namespace nex::rmc {

    class CompetitionRankingGetParam : public Structure {
    public:
        explicit CompetitionRankingGetParam(uint8_t minorVersion) : Structure(minorVersion), resultRange(minorVersion) {};
        CompetitionRankingGetParam(const CompetitionRankingGetParam& other) = default;
        CompetitionRankingGetParam(CompetitionRankingGetParam&& other) noexcept = default;
        ~CompetitionRankingGetParam() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto resultRangeData = resultRange.encode();
            data.insert(data.end(), resultRangeData.begin(), resultRangeData.end());
            auto festivalIdsData = festivalIds.encode();
            data.insert(data.end(), festivalIdsData.begin(), festivalIdsData.end());

            auto result = encodeHeader(COMPETITION_RANKING_GET_PARAM_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, COMPETITION_RANKING_GET_PARAM_VERSION);
            size += unk1.decode(data.subspan(size));
            size += resultRange.decode(data.subspan(size));
            size += festivalIds.decode(data.subspan(size));

            return size;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string result = "CompetitionRankingGetParam {\n";
            result += indent + "unk1: " + unk1.toString(true, indentation + 1) + "\n";
            result += indent + "resultRange: " + resultRange.toString(indentation + 1) + "\n";
            result += indent + "festivalIds: " + festivalIds.toString(indentation + 1) + "\n";
            result += last_indent + "}";
            return result;
        }

        [[nodiscard]] std::string getName() const override { return "CompetitionRankingGetParam"; }

        CompetitionRankingGetParam& operator=(const CompetitionRankingGetParam& other) = default;
        CompetitionRankingGetParam& operator=(CompetitionRankingGetParam&& other) noexcept = default;

        UInt32 unk1;
        ResultRange resultRange;
        List<UInt32> festivalIds;

    private:
        const inline static uint8_t COMPETITION_RANKING_GET_PARAM_VERSION = 1;
    };

} // namespace nex::rmc

#endif //COMPETITIONRANKINGGETPARAM_HPP
