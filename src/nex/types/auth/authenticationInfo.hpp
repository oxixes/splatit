#ifndef SPLATOON_SERVER_AUTHENTICATIONINFO_HPP
#define SPLATOON_SERVER_AUTHENTICATIONINFO_HPP

#include "../common/data.hpp"
#include "../common/string.hpp"
#include "../common/ints.hpp"

namespace nex::rmc {

    class AuthenticationInfo : public Data {
    public:
        explicit AuthenticationInfo(uint8_t minorVersion) : Data(minorVersion) {}
        AuthenticationInfo(const AuthenticationInfo& other) = default;
        AuthenticationInfo(AuthenticationInfo&& other) noexcept = default;
        ~AuthenticationInfo() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> parentData = Data::encode();
            std::vector<uint8_t> data;

            auto authTokenData = authToken.encode();
            data.insert(data.end(), authTokenData.begin(), authTokenData.end());

            auto versionData = version.encode();
            data.insert(data.end(), versionData.begin(), versionData.end());

            auto tokenTypeData = tokenType.encode();
            data.insert(data.end(), tokenTypeData.begin(), tokenTypeData.end());

            auto serverVersionData = serverVersion.encode();
            data.insert(data.end(), serverVersionData.begin(), serverVersionData.end());

            auto header = encodeHeader(AUTHENTICATION_INFO_VERSION, data.size());
            parentData.insert(parentData.end(), header.begin(), header.end());
            parentData.insert(parentData.end(), data.begin(), data.end());

            return parentData;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t parentSize = Data::decode(data);
            data = data.subspan(parentSize);

            size_t size = 0;
            size += decodeHeader(data, AUTHENTICATION_INFO_VERSION);
            size += authToken.decode(data.subspan(size));
            size += version.decode(data.subspan(size));
            size += tokenType.decode(data.subspan(size));
            size += serverVersion.decode(data.subspan(size));

            return size;
        }

        AuthenticationInfo& operator=(const AuthenticationInfo& other) = default;
        AuthenticationInfo& operator=(AuthenticationInfo&& other) noexcept = default;

        String authToken;
        UInt32 version;
        UInt8 tokenType;
        UInt32 serverVersion;

    private:
        const inline static uint8_t AUTHENTICATION_INFO_VERSION = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_AUTHENTICATIONINFO_HPP
