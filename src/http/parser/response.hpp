#ifndef SPLATOON_SERVER_HTTP_RESPONSE_HPP
#define SPLATOON_SERVER_HTTP_RESPONSE_HPP

#include <map>
#include <vector>
#include <string>
#include <span>
#include <memory>

#include "common.hpp"
#include "status_codes.hpp"

namespace http {

class Response {
public:
    Response() = default;
    explicit Response(Version version, int status = HTTP_STATUS_OK);
    Response(const Response&) = default;
    Response(Response&&) = default;

    static std::unique_ptr<Response> parse(const std::vector<uint8_t>& data, size_t& length, bool connectionClose,
                          bool reqWasHead);

    [[nodiscard]] Version getVersion() const;
    [[nodiscard]] int getStatus() const;

    [[nodiscard]] bool hasHeader(const std::string& key) const;
    [[nodiscard]] const std::vector<std::string>& getHeader(const std::string& key) const;
    void setHeader(const std::string& key, const std::string& value);
    void addHeader(const std::string& key, const std::string& value);

    [[nodiscard]] const std::vector<uint8_t>& getBody() const;
    void setBody(std::vector<uint8_t>& newBody);
    void setBody(std::vector<uint8_t>&& newBody);

    [[nodiscard]] std::vector<uint8_t> serialize() const;

    Response& operator=(const Response&) = default;
    Response& operator=(Response&&) = default;

private:
    Version version = Version::HTTP_1_1;
    int status = HTTP_STATUS_OK;
    std::unordered_map<std::string, std::vector<std::string>> headers;
    std::vector<uint8_t> body;

    static bool isHeaderComplete(const std::vector<uint8_t>& data, size_t& length);
    void parseHTTPHeader(const std::vector<uint8_t>& data, size_t length);
    bool isBodyComplete(const std::vector<uint8_t>& data, size_t& length, size_t headerLength, bool connectionClose,
                        bool reqWasHead);
    void parseHTTPBody(const std::vector<uint8_t>& data, size_t headerLength, size_t length);
};

} // namespace http

#endif //SPLATOON_SERVER_HTTP_RESPONSE_HPP
