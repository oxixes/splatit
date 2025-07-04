#ifndef SPLATOON_SERVER_JOINMATCHMAKESESSIONPARAM_HPP
#define SPLATOON_SERVER_JOINMATCHMAKESESSIONPARAM_HPP

#include "../common/structure.hpp"
#include "../common/ints.hpp"
#include "../common/list.hpp"
#include "../common/string.hpp"
#include "matchmakeBlockListParam.hpp"

namespace nex::rmc {

    class JoinMatchmakeSessionParam : public Structure {
    public:
        explicit JoinMatchmakeSessionParam(uint8_t minorVersion) : Structure(minorVersion) {};
        JoinMatchmakeSessionParam(const JoinMatchmakeSessionParam& other) = default;
        JoinMatchmakeSessionParam(JoinMatchmakeSessionParam&& other) noexcept = default;
        ~JoinMatchmakeSessionParam() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto gidData = gid.encode();
            data.insert(data.end(), gidData.begin(), gidData.end());
            auto additionalParticipantsData = additionalParticipants.encode();
            data.insert(data.end(), additionalParticipantsData.begin(), additionalParticipantsData.end());
            auto gidForParticipationCheckData = gidForParticipationCheck.encode();
            data.insert(data.end(), gidForParticipationCheckData.begin(), gidForParticipationCheckData.end());
            auto joinMatchmakeSessionOptionData = joinMatchmakeSessionOption.encode();
            data.insert(data.end(), joinMatchmakeSessionOptionData.begin(), joinMatchmakeSessionOptionData.end());
            auto joinMathcmakeSessionBehaviorData = joinMathcmakeSessionBehavior.encode();
            data.insert(data.end(), joinMathcmakeSessionBehaviorData.begin(), joinMathcmakeSessionBehaviorData.end());
            auto userPasswordData = userPassword.encode();
            data.insert(data.end(), userPasswordData.begin(), userPasswordData.end());
            auto systemPasswordData = systemPassword.encode();
            data.insert(data.end(), systemPasswordData.begin(), systemPasswordData.end());
            auto joinMessageData = joinMessage.encode();
            data.insert(data.end(), joinMessageData.begin(), joinMessageData.end());
            auto participationCountData = participationCount.encode();
            data.insert(data.end(), participationCountData.begin(), participationCountData.end());
//            auto extraParticipantsData = extraParticipants.encode();
//            data.insert(data.end(), extraParticipantsData.begin(), extraParticipantsData.end());
//            auto blockListParamData = blockListParam.encode();
//            data.insert(data.end(), blockListParamData.begin(), blockListParamData.end());

            auto result = encodeHeader(JOIN_MATCHMAKE_SESSION_PARAM_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, JOIN_MATCHMAKE_SESSION_PARAM_VERSION);
            size += gid.decode(data.subspan(size));
            size += additionalParticipants.decode(data.subspan(size));
            size += gidForParticipationCheck.decode(data.subspan(size));
            size += joinMatchmakeSessionOption.decode(data.subspan(size));
            size += joinMathcmakeSessionBehavior.decode(data.subspan(size));
            size += userPassword.decode(data.subspan(size));
            size += systemPassword.decode(data.subspan(size));
            size += joinMessage.decode(data.subspan(size));
            size += participationCount.decode(data.subspan(size));
//            size += extraParticipants.decode(data.subspan(size));
//            size += blockListParam.decode(data.subspan(size));

            return size;
        }

        JoinMatchmakeSessionParam& operator=(const JoinMatchmakeSessionParam& other) = default;
        JoinMatchmakeSessionParam& operator=(JoinMatchmakeSessionParam&& other) noexcept = default;

        UInt32 gid;
        List<PID> additionalParticipants;
        UInt32 gidForParticipationCheck;
        UInt32 joinMatchmakeSessionOption;
        UInt8 joinMathcmakeSessionBehavior;
        String userPassword;
        String systemPassword;
        String joinMessage;
        UInt16 participationCount;
//        UInt16 extraParticipants;
//        MatchmakeBlockListParam blockListParam;

    private:
        const inline static uint8_t JOIN_MATCHMAKE_SESSION_PARAM_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_JOINMATCHMAKESESSIONPARAM_HPP
