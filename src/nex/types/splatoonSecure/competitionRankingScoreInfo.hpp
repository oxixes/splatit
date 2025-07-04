#ifndef COMPETITIONRANKINGSCOREINFO_HPP
#define COMPETITIONRANKINGSCOREINFO_HPP

#include "competitionRankingScoreData.hpp"
#include "../common/structure.hpp"
#include "../common/list.hpp"

namespace nex::rmc {

    class CompetitionRankingScoreInfo : public Structure {
    public:
        explicit CompetitionRankingScoreInfo(uint8_t minorVersion) : Structure(minorVersion) {};
        CompetitionRankingScoreInfo(const CompetitionRankingScoreInfo& other) = default;
        CompetitionRankingScoreInfo(CompetitionRankingScoreInfo&& other) noexcept = default;
        ~CompetitionRankingScoreInfo() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto festivalIdData = festivalId.encode();
            data.insert(data.end(), festivalIdData.begin(), festivalIdData.end());
            auto scoreDataData = scoreData.encode();
            data.insert(data.end(), scoreDataData.begin(), scoreDataData.end());
            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto teamWinsData = teamWins.encode();
            data.insert(data.end(), teamWinsData.begin(), teamWinsData.end());
            auto teamVotesData = teamVotes.encode();
            data.insert(data.end(), teamVotesData.begin(), teamVotesData.end());

            auto result = encodeHeader(COMPETITION_RANKING_SCORE_INFO_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, COMPETITION_RANKING_SCORE_INFO_VERSION);
            size += festivalId.decode(data.subspan(size));
            size += scoreData.decode(data.subspan(size));
            size += unk1.decode(data.subspan(size));
            size += teamWins.decode(data.subspan(size));
            size += teamVotes.decode(data.subspan(size));

            return size;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string result = "CompetitionRankingScoreInfo {\n";
            result += indent + "festivalId: " + festivalId.toString(true, indentation + 1) + "\n";
            result += indent + "scoreData: " + scoreData.toString(indentation + 1) + "\n";
            result += indent + "unk1: " + unk1.toString(true, indentation + 1) + "\n";
            result += indent + "teamWins: " + teamWins.toString(indentation + 1) + "\n";
            result += indent + "teamVotes: " + teamVotes.toString(indentation + 1) + "\n";
            result += last_indent + "}";
            return result;
        }

        [[nodiscard]] std::string getName() const override { return "CompetitionRankingScoreInfo"; }

        CompetitionRankingScoreInfo& operator=(const CompetitionRankingScoreInfo& other) = default;
        CompetitionRankingScoreInfo& operator=(CompetitionRankingScoreInfo&& other) noexcept = default;

        UInt32 festivalId;
        List<CompetitionRankingScoreData> scoreData;
        UInt32 unk1; // Ignored by the game
        List<UInt32> teamWins;
        List<UInt32> teamVotes;

    private:
        const inline static uint8_t COMPETITION_RANKING_SCORE_INFO_VERSION = 0;
    };

} // namespace nex::rmc

#endif //COMPETITIONRANKINGSCOREINFO_HPP
