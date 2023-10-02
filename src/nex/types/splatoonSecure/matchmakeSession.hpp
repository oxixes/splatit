#ifndef SPLATOON_SERVER_MATCHMAKESESSION_HPP
#define SPLATOON_SERVER_MATCHMAKESESSION_HPP

#include "gathering.hpp"
#include "../common/list.hpp"
#include "../common/buffer.hpp"
#include "../common/datetime.hpp"
#include "matchmakeParam.hpp"

namespace nex::rmc {

    class MatchmakeSession : public Gathering {
    public:
        explicit MatchmakeSession(uint8_t minorVersion) : Gathering(minorVersion), matchmakeParam(minorVersion) {};
        ~MatchmakeSession() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Gathering::encode();

            std::vector<uint8_t> data;

            auto gameModeData = gameMode.encode();
            data.insert(data.end(), gameModeData.begin(), gameModeData.end());
            auto attributesData = attributes.encode();
            data.insert(data.end(), attributesData.begin(), attributesData.end());
            auto openParticipationData = openParticipation.encode();
            data.insert(data.end(), openParticipationData.begin(), openParticipationData.end());
            auto matchmakeSystemTypeData = matchmakeSystemType.encode();
            data.insert(data.end(), matchmakeSystemTypeData.begin(), matchmakeSystemTypeData.end());
            auto appBufferData = appBuffer.encode();
            data.insert(data.end(), appBufferData.begin(), appBufferData.end());
            auto participationCountData = participationCount.encode();
            data.insert(data.end(), participationCountData.begin(), participationCountData.end());
            auto progressScoreData = progressScore.encode();
            data.insert(data.end(), progressScoreData.begin(), progressScoreData.end());
            auto sessionKeyData = sessionKey.encode();
            data.insert(data.end(), sessionKeyData.begin(), sessionKeyData.end());
            auto option0Data = option0.encode();
            data.insert(data.end(), option0Data.begin(), option0Data.end());
            auto matchmakeParamData = matchmakeParam.encode();
            data.insert(data.end(), matchmakeParamData.begin(), matchmakeParamData.end());
            auto startedTimeData = startedTime.encode();
            data.insert(data.end(), startedTimeData.begin(), startedTimeData.end());
            auto userPasswordData = userPassword.encode();
            data.insert(data.end(), userPasswordData.begin(), userPasswordData.end());
            auto referGidData = referGid.encode();
            data.insert(data.end(), referGidData.begin(), referGidData.end());
            auto userPasswordEnabledData = userPasswordEnabled.encode();
            data.insert(data.end(), userPasswordEnabledData.begin(), userPasswordEnabledData.end());
            auto systemPasswordEnabledData = systemPasswordEnabled.encode();
            data.insert(data.end(), systemPasswordEnabledData.begin(), systemPasswordEnabledData.end());

            auto header = encodeHeader(MATCHMAKE_SESSION_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        [[nodiscard]] std::vector<uint8_t> encodeGathering() const {
            return Gathering::encode();
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += Gathering::decode(data.subspan(size));
            size += decodeHeader(data.subspan(size), MATCHMAKE_SESSION_VERSION);
            size += gameMode.decode(data.subspan(size));
            size += attributes.decode(data.subspan(size));
            size += openParticipation.decode(data.subspan(size));
            size += matchmakeSystemType.decode(data.subspan(size));
            size += appBuffer.decode(data.subspan(size));
            size += participationCount.decode(data.subspan(size));
            size += progressScore.decode(data.subspan(size));
            size += sessionKey.decode(data.subspan(size));
            size += option0.decode(data.subspan(size));
            size += matchmakeParam.decode(data.subspan(size));
            size += startedTime.decode(data.subspan(size));
            size += userPassword.decode(data.subspan(size));
            size += referGid.decode(data.subspan(size));
            size += userPasswordEnabled.decode(data.subspan(size));
            size += systemPasswordEnabled.decode(data.subspan(size));

            return size;
        }

        UInt32 gameMode;
        List<UInt32> attributes;
        Bool openParticipation;
        UInt32 matchmakeSystemType;
        Buffer appBuffer;
        UInt32 participationCount;
        UInt8 progressScore;
        Buffer sessionKey;
        UInt32 option0;
        MatchmakeParam matchmakeParam;
        Datetime startedTime;
        String userPassword;
        UInt32 referGid;
        Bool userPasswordEnabled;
        Bool systemPasswordEnabled;

    private:
        const inline static uint8_t MATCHMAKE_SESSION_VERSION = 3;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_MATCHMAKESESSION_HPP
