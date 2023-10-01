#ifndef SPLATOON_SERVER_PLAYINGSESSION_HPP
#define SPLATOON_SERVER_PLAYINGSESSION_HPP

#include "../common/structure.hpp"
#include "../common/ints.hpp"
#include "gathering.hpp"
#include "../common/anyDataHolder.hpp"

namespace nex::rmc {

    class PlayingSession : public Structure {
    public:
        explicit PlayingSession(uint8_t minorVersion) : Structure(minorVersion), gathering(minorVersion) {};
        ~PlayingSession() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto pidData = pid.encode();
            data.insert(data.end(), pidData.begin(), pidData.end());

            AnyDataHolder gatheringData(minorVersion);
            gatheringData.set(gathering, "Gathering");
            auto gatheringDataEncoded = gatheringData.encode();
            data.insert(data.end(), gatheringDataEncoded.begin(), gatheringDataEncoded.end());

            auto result = encodeHeader(PLAYING_SESSION_VERSION, 0);
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, PLAYING_SESSION_VERSION);
            size += pid.decode(data.subspan(size));

            AnyDataHolder gatheringData(minorVersion);
            size += gatheringData.decode(data.subspan(size));
            if (gatheringData.getType() != "Gathering")
                throw MalformedException("Expected Gathering when decoding PlayingSession, got " + gatheringData.getType());

            gathering = gatheringData.get<Gathering>();

            return size;
        }

        PID pid;
        Gathering gathering;

    private:
        const inline static uint8_t PLAYING_SESSION_VERSION = 0;
    };
}

#endif //SPLATOON_SERVER_PLAYINGSESSION_HPP
