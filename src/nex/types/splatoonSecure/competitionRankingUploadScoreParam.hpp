#ifndef SPLATOON_SERVER_COMPETITIONRANKINGUPLOADSCOREPARAM_HPP
#define SPLATOON_SERVER_COMPETITIONRANKINGUPLOADSCOREPARAM_HPP

#include "../common/structure.hpp"
#include "../common/list.hpp"
#include "../common/buffer.hpp"

namespace nex::rmc {

    class CompetitionRankingUploadScoreParam : public Structure {
    public:
        explicit CompetitionRankingUploadScoreParam(uint8_t minorVersion) : Structure(minorVersion) {};
        CompetitionRankingUploadScoreParam(const CompetitionRankingUploadScoreParam& other) = default;
        CompetitionRankingUploadScoreParam(CompetitionRankingUploadScoreParam&& other) noexcept = default;
        ~CompetitionRankingUploadScoreParam() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto festivalIdData = festivalId.encode();
            data.insert(data.end(), festivalIdData.begin(), festivalIdData.end());
            auto unk2Data = unk2.encode();
            data.insert(data.end(), unk2Data.begin(), unk2Data.end());
            auto scoreData = score.encode();
            data.insert(data.end(), scoreData.begin(), scoreData.end());
            auto teamIdData = teamId.encode();
            data.insert(data.end(), teamIdData.begin(), teamIdData.end());
            auto teamScoreData = teamScore.encode();
            data.insert(data.end(), teamScoreData.begin(), teamScoreData.end());
            auto isFirstUploadData = isFirstUpload.encode();
            data.insert(data.end(), isFirstUploadData.begin(), isFirstUploadData.end());
            auto appDataData = appData.encode();
            data.insert(data.end(), appDataData.begin(), appDataData.end());

            auto result = encodeHeader(COMPETITION_RANKING_UPLOAD_SCORE_PARAM_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, COMPETITION_RANKING_UPLOAD_SCORE_PARAM_VERSION);
            size += unk1.decode(data.subspan(size));
            size += festivalId.decode(data.subspan(size));
            size += unk2.decode(data.subspan(size));
            size += score.decode(data.subspan(size));
            size += teamId.decode(data.subspan(size));
            size += teamScore.decode(data.subspan(size));
            size += isFirstUpload.decode(data.subspan(size));
            size += appData.decode(data.subspan(size));

            return size;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string result = "CompetitionRankingUploadScoreParam {\n";
            result += indent + "unk1: " + unk1.toString(true, indentation + 1) + "\n";
            result += indent + "festivalId: " + festivalId.toString(true, indentation + 1) + "\n";
            result += indent + "unk2: " + unk2.toString(true, indentation + 1) + "\n";
            result += indent + "score: " + score.toString(true, indentation + 1) + "\n";
            result += indent + "teamId: " + teamId.toString(true, indentation + 1) + "\n";
            result += indent + "teamScore: " + teamScore.toString(true, indentation + 1) + "\n";
            result += indent + "isFirstUpload: " + isFirstUpload.toString(true, indentation + 1) + "\n";
            result += indent + "appData: " + appData.toString(indentation + 1) + "\n";
            result += last_indent + "}";
            return result;
        }

        [[nodiscard]] std::string getName() const override { return "CompetitionRankingUploadScoreParam"; }

        CompetitionRankingUploadScoreParam& operator=(const CompetitionRankingUploadScoreParam& other) = default;
        CompetitionRankingUploadScoreParam& operator=(CompetitionRankingUploadScoreParam&& other) noexcept = default;

        UInt32 unk1;
        UInt32 festivalId;
        UInt32 unk2;
        UInt32 score;
        UInt8 teamId;
        UInt32 teamScore;
        Bool isFirstUpload;
        qBuffer appData;

    private:
        const inline static uint8_t COMPETITION_RANKING_UPLOAD_SCORE_PARAM_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_COMPETITIONRANKINGUPLOADSCOREPARAM_HPP
