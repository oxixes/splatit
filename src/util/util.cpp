#include "util.hpp"

namespace util {

std::string getOpenSSLError() {
    char buff[512];
    ERR_error_string(ERR_get_error(), buff);
    return std::string{buff};
}

bool checkParentDirectory(const fs::path& filePath) {
    return fs::exists(filePath.parent_path())
        && fs::is_directory(filePath.parent_path());
}

} // namespace util