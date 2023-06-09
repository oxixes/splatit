#ifndef SPLATOON_SERVER_UTIL_HPP
#define SPLATOON_SERVER_UTIL_HPP

#include <string>
#include <filesystem>

#include <openssl/err.h>

namespace fs = std::filesystem;

namespace util {
    std::string getOpenSSLError(const unsigned long* error = nullptr);
    bool checkParentDirectory(const fs::path& filePath);
} // namespace util

#endif //SPLATOON_SERVER_UTIL_HPP
