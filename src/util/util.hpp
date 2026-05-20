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

    sockaddr_in ipv4ToSockAddr(sock::IPv4Addr dir);
    std::string ipv4ToString(sock::IPv4Addr dir);
    std::string ipv4WPortToString(sock::IPv4Addr dir);
    sock::IPv4Addr stringToIPv4(const std::string& str);
    sock::IPv4Addr stringToIPv4WPort(const std::string& str);

    std::string getDateHeader();
    std::string getDateHeader(time_t time);
    std::string getXNintendoDateHeader();
    std::string getDateISO8601(std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> time);

    void getu64Little(uint64_t& v);
    void getu32Little(uint32_t& v);
    void getu32Big(uint32_t& v);
    void getu16Little(uint16_t& v);
    void getu16Big(uint16_t& v);

    uint32_t getNextPowerOfTwo(uint32_t v);
    uint32_t pow2Align(uint32_t v, uint32_t align);

    std::vector<std::string> split(const std::string& str, const std::string& delim);
    std::string bin2hex(const std::vector<uint8_t>& data);

    std::string formatTime(std::chrono::system_clock::time_point time);
    std::string formatDate(std::chrono::system_clock::time_point time);
} // namespace util

#endif //SPLATOON_SERVER_UTIL_HPP
