#include "util.hpp"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <bit>
#include <chrono>

namespace util {

std::string getOpenSSLError(const unsigned long* error) {
    char buff[512];
    ERR_error_string((error == nullptr) ? ERR_get_error() : *error, buff);
    ERR_clear_error();

    return std::string{buff};
}

bool checkParentDirectory(const fs::path& filePath) {
    return fs::exists(filePath.parent_path())
        && fs::is_directory(filePath.parent_path());
}

#ifdef _WIN32
std::string getWSAError(int error) {
    char* buffer;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                   reinterpret_cast<char*>(&buffer), 0, nullptr);
    std::string message(buffer);
    LocalFree(buffer);
    return message;
}
#endif

sockaddr_in ipv4ToSockAddr(sock::IPv4Addr dir) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dir.port);
#ifdef _WIN32
    addr.sin_addr.S_un.S_un_b.s_b1 = dir.a;
    addr.sin_addr.S_un.S_un_b.s_b2 = dir.b;
    addr.sin_addr.S_un.S_un_b.s_b3 = dir.c;
    addr.sin_addr.S_un.S_un_b.s_b4 = dir.d;
#else
    addr.sin_addr.s_addr = htonl((dir.a << 24) | (dir.b << 16) | (dir.c << 8) | dir.d);
#endif
    return addr;
}

std::string ipv4ToString(sock::IPv4Addr dir) {
    return std::to_string(dir.a) + "." + std::to_string(dir.b) + "." + std::to_string(dir.c) + "." + std::to_string(dir.d);
}

sock::IPv4Addr stringToIPv4(const std::string& str) {
    sock::IPv4Addr addr{};
    std::vector<std::string> parts = split(str, ".");
    if (parts.size() != 4) {
        return addr;
    }

    addr.a = std::stoi(parts[0]);
    addr.b = std::stoi(parts[1]);
    addr.c = std::stoi(parts[2]);
    addr.d = std::stoi(parts[3]);
    addr.port = 0;
    return addr;
}

std::string getDateHeader() {
    return getDateHeader(time(nullptr));
}

std::string getDateHeader(time_t time) {
    char buff[128];
    tm* gmt = gmtime(&time);
    strftime(buff, sizeof(buff), "%a, %d %b %Y %H:%M:%S GMT", gmt);
    return std::string{buff};
}

std::string getXNintendoDateHeader() {
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(duration);
}

std::string getDateISO8601(std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> time) {
    char buff[128];
    std::time_t tt = std::chrono::system_clock::to_time_t(time);
    tm* gmt = gmtime(&tt);
    strftime(buff, sizeof(buff), "%Y-%m-%dT%H:%M:%S", gmt);
    return std::string{buff};
}

void getu64Little(uint64_t& v) {
    if (std::endian::native == std::endian::little) {
        return;
    }

    v = ((v & 0xFF00000000000000) >> 56) | ((v & 0x00FF000000000000) >> 40) | ((v & 0x0000FF0000000000) >> 24) | ((v & 0x000000FF00000000) >> 8) |
        ((v & 0x00000000FF000000) << 8) | ((v & 0x0000000000FF0000) << 24) | ((v & 0x000000000000FF00) << 40) | ((v & 0x00000000000000FF) << 56);
}

void getu32Little(uint32_t& v) {
    // If the host is little endian, return the value as-is
    if (std::endian::native == std::endian::little) {
        return;
    }

    v = ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) | ((v & 0x0000FF00) << 8) | ((v & 0x000000FF) << 24);
}

void getu32Big(uint32_t& v) {
    // If the host is big endian, return the value as-is
    if (std::endian::native == std::endian::big) {
        return;
    }

    v = ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) | ((v & 0x0000FF00) << 8) | ((v & 0x000000FF) << 24);
}

void getu16Little(uint16_t& v) {
    if (std::endian::native == std::endian::little) {
        return;
    }

    v = ((v & 0xFF00) >> 8) | ((v & 0x00FF) << 8);
}

void getu16Big(uint16_t& v) {
    if (std::endian::native == std::endian::big) {
        return;
    }

    v = ((v & 0xFF00) >> 8) | ((v & 0x00FF) << 8);
}

uint32_t getNextPowerOfTwo(uint32_t v) {
    if (v > 0x7FFFFFFF) return 0x80000000;
    int res = 1;
    while (res < v) res <<= 1;
    return res;
}

uint32_t pow2Align(uint32_t v, uint32_t align) {
    return (v + align - 1) & ~(align - 1);
}

std::vector<std::string> split(const std::string& str, const std::string& delim) {
    std::vector<std::string> tokens;
    size_t prev = 0, pos = 0;
    do {
        pos = str.find(delim, prev);
        if (pos == std::string::npos) pos = str.length();

        std::string token = str.substr(prev, pos - prev);
        if (!token.empty()) tokens.push_back(token);

        prev = pos + delim.length();
    } while (pos < str.length() && prev < str.length());

    return tokens;
}

std::string bin2hex(const std::vector<uint8_t>& data) {
    constexpr char alphabet[] = "0123456789abcdef";

    std::string hex;
    hex.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        hex.push_back(alphabet[byte >> 4]);
        hex.push_back(alphabet[byte & 0xF]);
    }
    return hex;
}

std::string formatTime(std::chrono::system_clock::time_point time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S+00:00");
    return ss.str();
}

} // namespace util