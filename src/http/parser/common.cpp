#include "common.hpp"

namespace http {

void parseHeader(const std::string_view& header, std::map<std::string, std::vector<std::string>>& headers) {
    size_t pos = header.find(':');
    if (pos == std::string::npos) {
        throw MalformedException("Header malformed");
    }

    std::string key = std::string(header.substr(0, pos));
    std::string_view value = header.substr(pos + 1);

    // Turn key to lowercase
    for (char& i : key) {
        if (i >= 'A' && i <= 'Z') {
            i += 32;
        }
    }

    // Remove leading and trailing spaces from value
    if (value[0] == ' ') value = value.substr(1);
    if (value[value.size() - 1] == ' ') value = value.substr(0, value.size() - 1);

    if (headers.find(key) == headers.end()) {
        headers.insert({key, std::vector<std::string>()});
    }

    headers[key].emplace_back(value);
}

} // namespace http