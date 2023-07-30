#ifndef SPLATOON_SERVER_UTIL_HPP
#define SPLATOON_SERVER_UTIL_HPP

#include <string>
#include <filesystem>
#include <vector>

#include <openssl/err.h>
#include "../socket/socket.hpp"

namespace fs = std::filesystem;

namespace util {
    std::string getOpenSSLError(const unsigned long* error = nullptr);
    bool checkParentDirectory(const fs::path& filePath);

#ifdef _WIN32
    std::string getWSAError(int error);
#endif

    sockaddr_in ipv4ToSockAddr(sock::IPv4Dir dir);

    std::string ipv4ToString(sock::IPv4Dir dir);

    std::string getDateHeader();
    std::string getDateHeader(time_t time);
    std::string getXNintendoDateHeader();

    void getu32Little(uint32_t& v);
    void getu32Big(uint32_t& v);
    void getu16Little(uint16_t& v);
    void getu16Big(uint16_t& v);

    std::vector<std::string> split(const std::string& str, const std::string& delim);
} // namespace util

#endif //SPLATOON_SERVER_UTIL_HPP
