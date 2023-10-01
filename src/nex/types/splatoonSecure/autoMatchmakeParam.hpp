#ifndef SPLATOON_SERVER_AUTOMATCHMAKEPARAM_HPP
#define SPLATOON_SERVER_AUTOMATCHMAKEPARAM_HPP

#include "../common/structure.hpp"
#include "matchmakeSession.hpp"
#include "matchmakeSessionSearchCriteria.hpp"

namespace nex::rmc {

    class AutoMatchmakeParam : public Structure {
    public:
        explicit AutoMatchmakeParam(uint8_t minorVersion) : Structure(minorVersion), srcMatchmakeSession(minorVersion),
                                                            searchCriteria(minorVersion) {};
        ~AutoMatchmakeParam() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto matchmakeSessionData = srcMatchmakeSession.encode();
            data.insert(data.end(), matchmakeSessionData.begin(), matchmakeSessionData.end());
            auto additionalParticipantsData = additionalParticipants.encode();
            data.insert(data.end(), additionalParticipantsData.begin(), additionalParticipantsData.end());
            auto gidForParitipationCheckData = gidForParitipationCheck.encode();
            data.insert(data.end(), gidForParitipationCheckData.begin(), gidForParitipationCheckData.end());
            auto autoMatchmakeOptionData = autoMatchmakeOption.encode();
            data.insert(data.end(), autoMatchmakeOptionData.begin(), autoMatchmakeOptionData.end());
            auto joinMessageData = joinMessage.encode();
            data.insert(data.end(), joinMessageData.begin(), joinMessageData.end());
            auto participationCountData = participationCount.encode();
            data.insert(data.end(), participationCountData.begin(), participationCountData.end());
            auto searchCriteriaData = searchCriteria.encode();
            data.insert(data.end(), searchCriteriaData.begin(), searchCriteriaData.end());
            auto targetGidsData = targetGids.encode();
            data.insert(data.end(), targetGidsData.begin(), targetGidsData.end());

            auto header = encodeHeader(AUTO_MATCHMAKE_PARAM_VERSION, data.size());
            data.insert(data.begin(), header.begin(), header.end());

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data.subspan(size), AUTO_MATCHMAKE_PARAM_VERSION);
            size += srcMatchmakeSession.decode(data.subspan(size));
            size += additionalParticipants.decode(data.subspan(size));
            size += gidForParitipationCheck.decode(data.subspan(size));
            size += autoMatchmakeOption.decode(data.subspan(size));
            size += joinMessage.decode(data.subspan(size));
            size += participationCount.decode(data.subspan(size));
            size += searchCriteria.decode(data.subspan(size));
            size += targetGids.decode(data.subspan(size));

            return size;
        }

        MatchmakeSession srcMatchmakeSession;
        List<PID> additionalParticipants;
        UInt32 gidForParitipationCheck;
        UInt32 autoMatchmakeOption;
        String joinMessage;
        UInt16 participationCount;
        List<MatchmakeSessionSearchCriteria> searchCriteria;
        List<UInt32> targetGids;

    private:
        const inline static uint8_t AUTO_MATCHMAKE_PARAM_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_AUTOMATCHMAKEPARAM_HPP
