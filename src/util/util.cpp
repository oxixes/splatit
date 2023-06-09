#include "util.hpp"

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

} // namespace util