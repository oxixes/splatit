#ifndef SPLATOON_SERVER_CREATEMATCHMAKESESSIONPARAM_HPP
#define SPLATOON_SERVER_CREATEMATCHMAKESESSIONPARAM_HPP

#include "../common/structure.hpp"
#include "matchmakeSession.hpp"

namespace nex::rmc {

    class CreateMatchmakeSessionParam : public Structure {
    public:
        explicit CreateMatchmakeSessionParam(uint8_t minorVersion) : Structure(minorVersion), srcMatchmakeSession(minorVersion) {};
        CreateMatchmakeSessionParam(const CreateMatchmakeSessionParam& other) = default;
        CreateMatchmakeSessionParam(CreateMatchmakeSessionParam&& other) noexcept = default;
        ~CreateMatchmakeSessionParam() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto srcMatchmakeSessionData = srcMatchmakeSession.encode();
            data.insert(data.end(), srcMatchmakeSessionData.begin(), srcMatchmakeSessionData.end());
            auto additionalParticipantsData = additionalParticipants.encode();
            data.insert(data.end(), additionalParticipantsData.begin(), additionalParticipantsData.end());
            auto gidForParticipationCheckData = gidForParticipationCheck.encode();
            data.insert(data.end(), gidForParticipationCheckData.begin(), gidForParticipationCheckData.end());
            auto createMatchmakeSessionOptionData = createMatchmakeSessionOption.encode();
            data.insert(data.end(), createMatchmakeSessionOptionData.begin(), createMatchmakeSessionOptionData.end());
            auto joinMessageData = joinMessage.encode();
            data.insert(data.end(), joinMessageData.begin(), joinMessageData.end());
            auto participationCountData = participationCount.encode();
            data.insert(data.end(), participationCountData.begin(), participationCountData.end());

            auto result = encodeHeader(CREATE_MATCHMAKE_SESSION_PARAM_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, CREATE_MATCHMAKE_SESSION_PARAM_VERSION);
            size += srcMatchmakeSession.decode(data.subspan(size));
            size += additionalParticipants.decode(data.subspan(size));
            size += gidForParticipationCheck.decode(data.subspan(size));
            size += createMatchmakeSessionOption.decode(data.subspan(size));
            size += joinMessage.decode(data.subspan(size));
            size += participationCount.decode(data.subspan(size));

            return size;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string result = "CreateMatchmakeSessionParam {\n";
            result += indent + "srcMatchmakeSession: " + srcMatchmakeSession.toString(indentation + 1) + "\n";
            result += indent + "additionalParticipants: " + additionalParticipants.toString(indentation + 1) + "\n";
            result += indent + "gidForParticipationCheck: " + gidForParticipationCheck.toString(true, indentation + 1) + "\n";
            result += indent + "createMatchmakeSessionOption: " + createMatchmakeSessionOption.toString(true, indentation + 1) + "\n";
            result += indent + "joinMessage: " + joinMessage.toString(indentation + 1) + "\n";
            result += indent + "participationCount: " + participationCount.toString(true, indentation + 1) + "\n";
            result += last_indent + "}";
            return result;
        }

        [[nodiscard]] std::string getName() const override { return "CreateMatchmakeSessionParam"; }

        CreateMatchmakeSessionParam& operator=(const CreateMatchmakeSessionParam& other) = default;
        CreateMatchmakeSessionParam& operator=(CreateMatchmakeSessionParam&& other) noexcept = default;

        MatchmakeSession srcMatchmakeSession;
        List<PID> additionalParticipants;
        UInt32 gidForParticipationCheck;
        UInt32 createMatchmakeSessionOption;
        String joinMessage;
        UInt16 participationCount;

    private:
        const inline static uint8_t CREATE_MATCHMAKE_SESSION_PARAM_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_CREATEMATCHMAKESESSIONPARAM_HPP
