#include "util.hpp"

#include <winsock2.h>

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

} // namespace util