#ifndef SPLATOON_SERVER_COMMON_HPP
#define SPLATOON_SERVER_COMMON_HPP

#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace http {

enum class Version {
    HTTP_1_0,
    HTTP_1_1
};

bool isHTTPHeaderComplete(const std::vector<uint8_t>& data, size_t& length);

void parseQuery(std::string_view queryStr, std::unordered_map<std::string, std::string>& queries);
void parseHeader(const std::string_view& header, std::unordered_map<std::string, std::vector<std::string>>& headers,
                 bool fromChunked = false);
bool isChunkedComplete(const std::vector<uint8_t>& data, size_t headerLength, size_t& length);
void parseChunked(const std::vector<uint8_t>& data, std::unordered_map<std::string, std::vector<std::string>>& headers,
                  size_t length, size_t headerLength, std::vector<uint8_t>& body);
std::string getFinalTransferEncoding(const std::string& transferEncoding);
bool isHTTPBodyComplete(const std::vector<uint8_t>& data, size_t& length, size_t headerLength,
                        std::unordered_map<std::string, std::vector<std::string>>& headers,
                        bool isResponse = false, int status = 0, bool reqWasHead = false,
                        bool connectionClose = false);

std::string percentDecode(const std::string& str);
std::string percentEncode(const std::string& str, bool allowSlash = false);

} // namespace http

#endif //SPLATOON_SERVER_COMMON_HPP
