#ifndef SPLATOON_SERVER_EXCEPTIONS_HPP
#define SPLATOON_SERVER_EXCEPTIONS_HPP

#include <stdexcept>

class MalformedException : public std::runtime_error {
public:
    explicit MalformedException(const std::string& what_arg) : std::runtime_error(what_arg) {};
    explicit MalformedException(const char* what_arg) : std::runtime_error(what_arg) {};
};

class NotCompleteException : public std::runtime_error {
public:
    explicit NotCompleteException(const std::string& what_arg) : std::runtime_error(what_arg) {};
    explicit NotCompleteException(const char* what_arg) : std::runtime_error(what_arg) {};
};

namespace sock {

    class RetryableException : public std::runtime_error {
    public:
        explicit RetryableException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit RetryableException(const char* what_arg) : std::runtime_error(what_arg) {};
    };

    class FatalException : public std::runtime_error {
    public:
        explicit FatalException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit FatalException(const char* what_arg) : std::runtime_error(what_arg) {};
    };

    class SSLException : public std::runtime_error {
    public:
        explicit SSLException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit SSLException(const char* what_arg) : std::runtime_error(what_arg) {};
    };

} // namespace sock

namespace http {

    class LengthUnknownException : public std::runtime_error {
    public:
        explicit LengthUnknownException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit LengthUnknownException(const char* what_arg) : std::runtime_error(what_arg) {};
    };

    class VersionNotSupportedException : public std::runtime_error {
    public:
        explicit VersionNotSupportedException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit VersionNotSupportedException(const char* what_arg) : std::runtime_error(what_arg) {};
    };

    class MethodNotSupportedException : public std::runtime_error {
    public:
        explicit MethodNotSupportedException(const std::string& what_arg) : std::runtime_error(what_arg) {};
        explicit MethodNotSupportedException(const char* what_arg) : std::runtime_error(what_arg) {};
    };

} // namespace http

#endif //SPLATOON_SERVER_EXCEPTIONS_HPP
