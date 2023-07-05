#ifndef SPLATOON_SERVER_RESPONSE_HPP
#define SPLATOON_SERVER_RESPONSE_HPP

#include <map>
#include <vector>
#include <string>

#include "common.hpp"
#include "status_codes.hpp"

namespace http {

class Response {
public:
    explicit Response(Version version, int status = HTTP_STATUS_OK);
    static Response parse(const std::vector<unsigned char>& data, size_t& length, bool connectionClose,
                          bool reqWasHead);

    [[nodiscard]] Version getVersion() const;
    [[nodiscard]] int getStatus() const;

    [[nodiscard]] bool hasHeader(const std::string& key) const;
    [[nodiscard]] const std::vector<std::string>& getHeader(const std::string& key) const;
    void setHeader(const std::string& key, const std::string& value);
    void addHeader(const std::string& key, const std::string& value);

    [[nodiscard]] const std::vector<unsigned char>& getBody() const;
    void setBody(std::vector<unsigned char>& newBody);

    [[nodiscard]] std::vector<unsigned char> serialize() const;

private:
    Response() = default;

    Version version = Version::HTTP_1_1;
    int status = HTTP_STATUS_OK;
    std::map<std::string, std::vector<std::string>> headers;
    std::vector<unsigned char> body;

    static bool isHeaderComplete(const std::vector<unsigned char>& data, size_t& length);
    void parseHTTPHeader(const std::vector<unsigned char>& data, size_t length);
    bool isBodyComplete(const std::vector<unsigned char>& data, size_t& length, size_t headerLength, bool connectionClose,
                        bool reqWasHead);
    void parseHTTPBody(const std::vector<unsigned char>& data, size_t headerLength, size_t length);
};

} // namespace http

#endif //SPLATOON_SERVER_RESPONSE_HPP
