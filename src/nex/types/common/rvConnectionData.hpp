#ifndef SPLATOON_SERVER_RVCONNECTIONDATA_HPP
#define SPLATOON_SERVER_RVCONNECTIONDATA_HPP

#include "structure.hpp"
#include "stationURL.hpp"
#include "buffer.hpp"

namespace nex::rmc {

    class RVConnectionData : public Structure {
    public:
        explicit RVConnectionData(uint8_t minorVersion) : Structure(minorVersion),
                                                          urlRegularProtocols(minorVersion),
                                                          lstSpecialProtocols(minorVersion),
                                                          urlSpecialProtocols(minorVersion) {};
        RVConnectionData(const RVConnectionData& other) = default;
        RVConnectionData(RVConnectionData&& other) noexcept = default;
        ~RVConnectionData() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto regularProtocols = urlRegularProtocols.encode();
            auto bufSpecialProtocols = lstSpecialProtocols.encode();
            auto specialProtocols = urlSpecialProtocols.encode();

            size_t length = regularProtocols.size() + bufSpecialProtocols.size() + specialProtocols.size();

            data.insert(data.end(), regularProtocols.begin(), regularProtocols.end());
            data.insert(data.end(), bufSpecialProtocols.begin(), bufSpecialProtocols.end());
            data.insert(data.end(), specialProtocols.begin(), specialProtocols.end());

            auto header = encodeHeader(RV_CONNECTION_DATA_VERSION, length);
            data.insert(data.begin(), header.begin(), header.end());

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t headerSize = decodeHeader(data, RV_CONNECTION_DATA_VERSION);
            data = data.subspan(headerSize);

            size_t regularProtocolsSize = urlRegularProtocols.decode(data);
            data = data.subspan(regularProtocolsSize);
            size_t bufSpecialProtocolsSize = lstSpecialProtocols.decode(data);
            data = data.subspan(bufSpecialProtocolsSize);
            size_t specialProtocolsSize = urlSpecialProtocols.decode(data);

            return headerSize + regularProtocolsSize + bufSpecialProtocolsSize + specialProtocolsSize;
        }

        RVConnectionData& operator=(const RVConnectionData& other) = default;
        RVConnectionData& operator=(RVConnectionData&& other) noexcept = default;

        StationURL urlRegularProtocols;
        Buffer lstSpecialProtocols;
        StationURL urlSpecialProtocols;

    private:
        constexpr static uint8_t RV_CONNECTION_DATA_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_RVCONNECTIONDATA_HPP
