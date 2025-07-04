#ifndef SPLATOON_SERVER_MATCHMAKESESSIONSEARCHCRITERIA_HPP
#define SPLATOON_SERVER_MATCHMAKESESSIONSEARCHCRITERIA_HPP

#include "../common/structure.hpp"
#include "../common/string.hpp"
#include "../common/list.hpp"
#include "matchmakeParam.hpp"

namespace nex::rmc {

    class MatchmakeSessionSearchCriteria : public Structure {
    public:
        explicit MatchmakeSessionSearchCriteria(uint8_t minorVersion) : Structure(minorVersion), matchmakeParam(minorVersion) {};
        MatchmakeSessionSearchCriteria(const MatchmakeSessionSearchCriteria& other) = default;
        MatchmakeSessionSearchCriteria(MatchmakeSessionSearchCriteria&& other) noexcept = default;
        ~MatchmakeSessionSearchCriteria() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto attributesData = attributes.encode();
            data.insert(data.end(), attributesData.begin(), attributesData.end());
            auto gameModeData = gameMode.encode();
            data.insert(data.end(), gameModeData.begin(), gameModeData.end());
            auto minParticipantsData = minParticipants.encode();
            data.insert(data.end(), minParticipantsData.begin(), minParticipantsData.end());
            auto maxParticipantsData = maxParticipants.encode();
            data.insert(data.end(), maxParticipantsData.begin(), maxParticipantsData.end());
            auto matchmakeSystemTypeData = matchmakeSystemType.encode();
            data.insert(data.end(), matchmakeSystemTypeData.begin(), matchmakeSystemTypeData.end());
            auto vacantOnlyData = vacantOnly.encode();
            data.insert(data.end(), vacantOnlyData.begin(), vacantOnlyData.end());
            auto excludeLockedData = excludeLocked.encode();
            data.insert(data.end(), excludeLockedData.begin(), excludeLockedData.end());
            auto excludeNonHostPidData = excludeNonHostPid.encode();
            data.insert(data.end(), excludeNonHostPidData.begin(), excludeNonHostPidData.end());
            auto selectionMethodData = selectionMethod.encode();
            data.insert(data.end(), selectionMethodData.begin(), selectionMethodData.end());
            auto vacantParticipantsData = vacantParticipants.encode();
            data.insert(data.end(), vacantParticipantsData.begin(), vacantParticipantsData.end());
            auto matchmakeParamData = matchmakeParam.encode();
            data.insert(data.end(), matchmakeParamData.begin(), matchmakeParamData.end());
            auto excludeUserPasswordSetData = excludeUserPasswordSet.encode();
            data.insert(data.end(), excludeUserPasswordSetData.begin(), excludeUserPasswordSetData.end());
            auto excludeSystemPasswordSetData = excludeSystemPasswordSet.encode();
            data.insert(data.end(), excludeSystemPasswordSetData.begin(), excludeSystemPasswordSetData.end());
            auto referGidData = referGid.encode();
            data.insert(data.end(), referGidData.begin(), referGidData.end());

            auto result = encodeHeader(MATCHMAKE_SESSION_SEARCH_CRITERIA_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t pos = 0;

            pos += decodeHeader(data, MATCHMAKE_SESSION_SEARCH_CRITERIA_VERSION);
            pos += attributes.decode(data.subspan(pos));
            pos += gameMode.decode(data.subspan(pos));
            pos += minParticipants.decode(data.subspan(pos));
            pos += maxParticipants.decode(data.subspan(pos));
            pos += matchmakeSystemType.decode(data.subspan(pos));
            pos += vacantOnly.decode(data.subspan(pos));
            pos += excludeLocked.decode(data.subspan(pos));
            pos += excludeNonHostPid.decode(data.subspan(pos));
            pos += selectionMethod.decode(data.subspan(pos));
            pos += vacantParticipants.decode(data.subspan(pos));
            pos += matchmakeParam.decode(data.subspan(pos));
            pos += excludeUserPasswordSet.decode(data.subspan(pos));
            pos += excludeSystemPasswordSet.decode(data.subspan(pos));
            pos += referGid.decode(data.subspan(pos));

            return pos;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string result = "MatchmakeSessionSearchCriteria {\n";
            result += indent + "attributes: " + attributes.toString(indentation + 1) + "\n";
            result += indent + "gameMode: " + gameMode.toString(indentation + 1) + "\n";
            result += indent + "minParticipants: " + minParticipants.toString(indentation + 1) + "\n";
            result += indent + "maxParticipants: " + maxParticipants.toString(indentation + 1) + "\n";
            result += indent + "matchmakeSystemType: " + matchmakeSystemType.toString(indentation + 1) + "\n";
            result += indent + "vacantOnly: " + vacantOnly.toString(true, indentation + 1) + "\n";
            result += indent + "excludeLocked: " + excludeLocked.toString(true, indentation + 1) + "\n";
            result += indent + "excludeNonHostPid: " + excludeNonHostPid.toString(true, indentation + 1) + "\n";
            result += indent + "selectionMethod: " + selectionMethod.toString(true, indentation + 1) + "\n";
            result += indent + "vacantParticipants: " + vacantParticipants.toString(true, indentation + 1) + "\n";
            result += indent + "matchmakeParam: " + matchmakeParam.toString(indentation + 1) + "\n";
            result += indent + "excludeUserPasswordSet: " + excludeUserPasswordSet.toString(true, indentation + 1) + "\n";
            result += indent + "excludeSystemPasswordSet: " + excludeSystemPasswordSet.toString(true, indentation + 1) + "\n";
            result += indent + "referGid: " + referGid.toString(true, indentation + 1) + "\n";
            result += last_indent + "}";
            return result;
        }

        [[nodiscard]] std::string getName() const override { return "MatchmakeSessionSearchCriteria"; }

        MatchmakeSessionSearchCriteria& operator=(const MatchmakeSessionSearchCriteria& other) = default;
        MatchmakeSessionSearchCriteria& operator=(MatchmakeSessionSearchCriteria&& other) noexcept = default;

        List<String> attributes;
        String gameMode;
        String minParticipants;
        String maxParticipants;
        String matchmakeSystemType;
        Bool vacantOnly;
        Bool excludeLocked;
        Bool excludeNonHostPid;
        UInt32 selectionMethod;
        UInt16 vacantParticipants;
        MatchmakeParam matchmakeParam;
        Bool excludeUserPasswordSet;
        Bool excludeSystemPasswordSet;
        UInt32 referGid;

    private:
        const inline static uint8_t MATCHMAKE_SESSION_SEARCH_CRITERIA_VERSION = 3;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_MATCHMAKESESSIONSEARCHCRITERIA_HPP
