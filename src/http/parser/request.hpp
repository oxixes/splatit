#ifndef SPLATOON_SERVER_HTTP_REQUEST_HPP
#define SPLATOON_SERVER_HTTP_REQUEST_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <span>
#include <memory>

#include "common.hpp"

namespace http {

enum class Method {
    M_GET,
    M_POST,
    M_PUT,
    M_DELETE,
    M_HEAD,
    M_OPTIONS
};

class Request {
public:
    Request() = default;
    explicit Request(const std::string& path, Method method = Method::M_GET, Version version = Version::HTTP_1_1);
    Request(const Request&) = default;
    Request(Request&&) = default;

    static std::unique_ptr<Request> parse(const std::vector<uint8_t>& data, size_t& length);

    [[nodiscard]] Method getMethod() const;
    [[nodiscard]] Version getVersion() const;
    [[nodiscard]] std::string getPath() const;

    [[nodiscard]] bool hasQuery(const std::string& key) const;
    [[nodiscard]] std::string getQuery(const std::string& key) const;
    void setQuery(const std::string& key, const std::string& value);

    [[nodiscard]] bool hasHeader(const std::string& key) const;
    [[nodiscard]] const std::vector<std::string>& getHeader(const std::string& key) const;
    void setHeader(const std::string& key, const std::string& value);
    void addHeader(const std::string& key, const std::string& value);

    [[nodiscard]] const std::vector<uint8_t>& getBody() const;
    void setBody(std::vector<uint8_t>& newBody);
    void setBody(std::vector<uint8_t>&& newBody);

    [[nodiscard]] std::vector<uint8_t> serialize() const;

    Request& operator=(const Request&) = default;
    Request& operator=(Request&&) = default;

private:
    Method method = Method::M_GET;
    Version version = Version::HTTP_1_1;
    std::string path;
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::vector<std::string>> headers;
    std::vector<uint8_t> body;

    static bool isHeaderComplete(const std::vector<uint8_t>& data, size_t& length);
    void parseHTTPHeader(const std::vector<uint8_t>& data, size_t length);
    bool isBodyComplete(const std::vector<uint8_t>& data, size_t& length, size_t headerLength);
    void parseHTTPBody(const std::vector<uint8_t>& data, size_t headerLength, size_t length);
};

} // namespace http

#endif //SPLATOON_SERVER_HTTP_REQUEST_HPP
