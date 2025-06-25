#ifndef COMPETITIONRANKINGSCOREDATA_HPP
#define COMPETITIONRANKINGSCOREDATA_HPP

#include "../common/buffer.hpp"
#include "../common/datetime.hpp"
#include "../common/structure.hpp"
#include "../common/list.hpp"

namespace nex::rmc {

    class CompetitionRankingScoreData : public Structure {
    public:
        explicit CompetitionRankingScoreData(uint8_t minorVersion) : Structure(minorVersion) {};
        ~CompetitionRankingScoreData() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto unk1Data = unk1.encode();
            data.insert(data.end(), unk1Data.begin(), unk1Data.end());
            auto userIdData = userId.encode();
            data.insert(data.end(), userIdData.begin(), userIdData.end());
            auto unk3Data = unk3.encode();
            data.insert(data.end(), unk3Data.begin(), unk3Data.end());
            auto uploadDateData = uploadDate.encode();
            data.insert(data.end(), uploadDateData.begin(), uploadDateData.end());
            auto unk4Data = unk4.encode();
            data.insert(data.end(), unk4Data.begin(), unk4Data.end());
            auto metadataData = metadata.encode();
            data.insert(data.end(), metadataData.begin(), metadataData.end());

            auto result = encodeHeader(COMPETITION_RANKING_SCORE_DATA_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, COMPETITION_RANKING_SCORE_DATA_VERSION);
            size += unk1.decode(data.subspan(size));
            size += userId.decode(data.subspan(size));
            size += unk3.decode(data.subspan(size));
            size += uploadDate.decode(data.subspan(size));
            size += unk4.decode(data.subspan(size));
            size += metadata.decode(data.subspan(size));

            return size;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string result = "CompetitionRankingScoreData {\n";
            result += indent + "unk1: " + unk1.toString(true, indentation + 1) + "\n";
            result += indent + "userId: " + userId.toString(true, indentation + 1) + "\n";
            result += indent + "unk3: " + unk3.toString(true, indentation + 1) + "\n";
            result += indent + "uploadDate: " + uploadDate.toString(indentation + 1) + "\n";
            result += indent + "unk4: " + unk4.toString(true, indentation + 1) + "\n";
            result += indent + "metadata: " + metadata.toString(indentation + 1) + "\n";
            result += last_indent + "}";
            return result;
        }

        [[nodiscard]] std::string getName() const override { return "CompetitionRankingScoreData"; }

        UInt32 unk1;
        PID userId;
        UInt32 unk3;
        Datetime uploadDate; // Guess, ignored by the game
        Bool unk4;
        qBuffer metadata;

    private:
        const inline static uint8_t COMPETITION_RANKING_SCORE_DATA_VERSION = 0;
    };

} // namespace nex::rmc

#endif //COMPETITIONRANKINGSCOREDATA_HPP
