#ifndef SPLATOON_SERVER_COMMON_HPP
#define SPLATOON_SERVER_COMMON_HPP

#include <stdexcept>
#include <map>
#include <vector>

namespace http {

enum class Version {
    HTTP_1_0,
    HTTP_1_1
};

class NotCompleteException : public std::runtime_error {
    public:
        explicit NotCompleteException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit NotCompleteException(const char* what_arg) : std::runtime_error(what_arg) {};
};

class LengthUnknownException : public std::runtime_error {
    public:
        explicit LengthUnknownException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit LengthUnknownException(const char* what_arg) : std::runtime_error(what_arg) {};
};

class MalformedException : public std::runtime_error {
    public:
        explicit MalformedException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit MalformedException(const char* what_arg) : std::runtime_error(what_arg) {};
};

class VersionNotSupportedException : public std::runtime_error {
    public:
        explicit VersionNotSupportedException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit VersionNotSupportedException(const char* what_arg) : std::runtime_error(what_arg) {};
};

void parseHeader(const std::string_view& header, std::map<std::string, std::vector<std::string>>& headers);

} // namespace http

#endif //SPLATOON_SERVER_COMMON_HPP
