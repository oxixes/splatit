#ifndef SPLATOON_SERVER_GATHERING_HPP
#define SPLATOON_SERVER_GATHERING_HPP

#include "../common/structure.hpp"
#include "../common/ints.hpp"
#include "../common/string.hpp"

namespace nex::rmc {

    class Gathering : public Structure {
    public:
        explicit Gathering(uint8_t minorVersion) : Structure(minorVersion) {};
        Gathering(const Gathering& other) = default;
        Gathering(Gathering&& other) noexcept = default;
        ~Gathering() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto idData = id.encode();
            data.insert(data.end(), idData.begin(), idData.end());
            auto ownerPidData = ownerPid.encode();
            data.insert(data.end(), ownerPidData.begin(), ownerPidData.end());
            auto hostPidData = hostPid.encode();
            data.insert(data.end(), hostPidData.begin(), hostPidData.end());
            auto minParticipantsData = minParticipants.encode();
            data.insert(data.end(), minParticipantsData.begin(), minParticipantsData.end());
            auto maxParticipantsData = maxParticipants.encode();
            data.insert(data.end(), maxParticipantsData.begin(), maxParticipantsData.end());
            auto participationPolicyData = participationPolicy.encode();
            data.insert(data.end(), participationPolicyData.begin(), participationPolicyData.end());
            auto policyArgumentData = policyArgument.encode();
            data.insert(data.end(), policyArgumentData.begin(), policyArgumentData.end());
            auto flagsData = flags.encode();
            data.insert(data.end(), flagsData.begin(), flagsData.end());
            auto stateData = state.encode();
            data.insert(data.end(), stateData.begin(), stateData.end());
            auto descriptionData = description.encode();
            data.insert(data.end(), descriptionData.begin(), descriptionData.end());

            auto result = encodeHeader(GATHERING_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, GATHERING_VERSION);
            size += id.decode(data.subspan(size));
            size += ownerPid.decode(data.subspan(size));
            size += hostPid.decode(data.subspan(size));
            size += minParticipants.decode(data.subspan(size));
            size += maxParticipants.decode(data.subspan(size));
            size += participationPolicy.decode(data.subspan(size));
            size += policyArgument.decode(data.subspan(size));
            size += flags.decode(data.subspan(size));
            size += state.decode(data.subspan(size));
            size += description.decode(data.subspan(size));

            return size;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string str = "Gathering {\n";
            str += indent + "id: " + id.toString(true, indentation + 1) + "\n";
            str += indent + "ownerPid: " + ownerPid.toString(true, indentation + 1) + "\n";
            str += indent + "hostPid: " + hostPid.toString(true, indentation + 1) + "\n";
            str += indent + "minParticipants: " + minParticipants.toString(true, indentation + 1) + "\n";
            str += indent + "maxParticipants: " + maxParticipants.toString(true, indentation + 1) + "\n";
            str += indent + "participationPolicy: " + participationPolicy.toString(true, indentation + 1) + "\n";
            str += indent + "policyArgument: " + policyArgument.toString(true, indentation + 1) + "\n";
            str += indent + "flags: " + flags.toString(true, indentation + 1) + "\n";
            str += indent + "state: " + state.toString(true, indentation + 1) + "\n";
            str += indent + "description: " + description.toString(indentation + 1) + "\n";
            str += last_indent + "}";
            return str;
        }

        [[nodiscard]] std::string getName() const override { return "Gathering"; }

        Gathering& operator=(const Gathering& other) = default;
        Gathering& operator=(Gathering&& other) noexcept = default;

        UInt32 id;
        PID ownerPid;
        PID hostPid;
        UInt16 minParticipants;
        UInt16 maxParticipants;
        UInt32 participationPolicy;
        UInt32 policyArgument;
        UInt32 flags;
        UInt32 state;
        String description;

    private:
        const inline static uint8_t GATHERING_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_GATHERING_HPP
