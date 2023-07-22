#include "util.hpp"

#include <winsock2.h>
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

std::string ipv4ToString(sock::IPv4Dir dir) {
    return std::to_string(dir.a) + "." + std::to_string(dir.b) + "." + std::to_string(dir.c) + "." + std::to_string(dir.d);
}

std::string getDateHeader() {
    return std::move(getDateHeader(time(nullptr)));
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

    return std::move(tokens);
}

} // namespace util