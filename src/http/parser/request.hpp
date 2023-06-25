#ifndef SPLATOON_SERVER_REQUEST_HPP
#define SPLATOON_SERVER_REQUEST_HPP

#include <string>
#include <vector>
#include <map>
#include <stdexcept>

#include "common.hpp"

namespace http {

enum class Method {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD
};

class MethodNotSupportedException : public std::runtime_error {
    public:
        explicit MethodNotSupportedException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit MethodNotSupportedException(const char* what_arg) : std::runtime_error(what_arg) {};
};

class Request {
public:
    Request(const std::string& path, Method method = Method::GET, Version version = Version::HTTP_1_1);
    static Request parse(const std::vector<unsigned char> data, size_t& length);

    Method getMethod() const;
    Version getVersion() const;
    std::string getPath() const;

    bool hasQuery(const std::string& key) const;
    std::string getQuery(const std::string& key) const;
    void setQuery(const std::string& key, const std::string& value);

    bool hasHeader(const std::string& key) const;
    const std::vector<std::string>& getHeader(const std::string& key) const;
    void setHeader(const std::string& key, const std::string& value);
    void addHeader(const std::string& key, const std::string& value);

    const std::vector<unsigned char>& getBody() const;

    std::vector<unsigned char> serialize() const;

private:
    Request() = default;

    Method method = Method::GET;
    Version version = Version::HTTP_1_1;
    std::string path;
    std::map<std::string, std::string> query;
    std::map<std::string, std::vector<std::string>> headers;
    std::vector<unsigned char> body;

    static bool isHeaderComplete(const std::vector<unsigned char>& data, size_t& length);
    void parseHTTPHeader(const std::vector<unsigned char>& data);
    bool isBodyComplete(const std::vector<unsigned char>& data, size_t& length, size_t headerLength,
                        bool& unknownLength);
};

} // namespace http

#endif //SPLATOON_SERVER_REQUEST_HPP
