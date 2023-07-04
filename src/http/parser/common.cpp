#include "common.hpp"

#include <sstream>

namespace http {

bool isHTTPHeaderComplete(const std::vector<unsigned char>& data, size_t& length) {
    std::string_view dataView(reinterpret_cast<const char*>(data.data()), data.size());
    size_t pos = dataView.find("\r\n\r\n");
    if (pos == std::string_view::npos) return false;

    length = pos + 4;
    return true;
}

void parseHeader(const std::string_view& header, std::map<std::string, std::vector<std::string>>& headers, bool fromChunked) {
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

    if (fromChunked) {
        if (key == "transfer-encoding" || key == "content-length" || key == "host" || key == "cache-control" ||
            key == "max-forwards" || key == "te" || key == "authorization" || key == "set-cookie" ||
            key == "content-encoding" || key == "content-type" || key == "content-range" || key == "trailer") {
            throw MalformedException("Header not allowed in chunk trailer");
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

bool isChunkedComplete(const std::vector<unsigned char>& data, size_t headerLength, size_t& length) {
    std::string_view dataView(reinterpret_cast<const char*>(data.data() + headerLength), data.size() - headerLength);
    size_t pos = 0;
    while (!dataView.empty()) {
        std::string_view chunkHeaderStr = dataView.substr(0, dataView.find("\r\n"));
        if (chunkHeaderStr.empty()) return false;
        std::string_view chunkSizeStr = chunkHeaderStr.substr(0, chunkHeaderStr.find(';'));

        try {
            size_t chunkSize = std::stoul(std::string(chunkSizeStr), nullptr, 16);
            if (chunkSize == 0) {
                if (dataView.find("\r\n\r\n") == std::string_view::npos) return false;

                length = pos + dataView.find("\r\n\r\n") + 4;
                return true;
            } else {
                if (dataView.size() < chunkHeaderStr.size() + 2 + chunkSize + 2) return false;
                if (dataView.substr(chunkHeaderStr.size() + 2 + chunkSize, 2) != "\r\n") {
                    throw MalformedException("Chunk size mismatch");
                }

                pos += chunkHeaderStr.size() + 2;
                pos += chunkSize + 2;
                dataView.remove_prefix(chunkHeaderStr.size() + 2);
                dataView.remove_prefix(chunkSize + 2);
            }
        } catch (std::invalid_argument&) {
            throw MalformedException("Chunk size is not a number");
        } catch (std::out_of_range&) {
            throw MalformedException("Chunk length too big");
        }
    }

    return false;
}

void parseChunked(const std::vector<unsigned char>& data, std::map<std::string, std::vector<std::string>>& headers,
                  size_t length, size_t headerLength, std::vector<unsigned char>& body) {
    size_t pos = headerLength;
    while (pos < headerLength + length) {
        auto end = data.begin() + (long long) headerLength + (long long) length;

        std::string delimiterStr = "\r\n";
        auto chunkHeaderEnd = std::search(data.begin() + (long long) pos, end,
                                          delimiterStr.begin(), delimiterStr.end());

        if (chunkHeaderEnd == end) throw MalformedException("Chunk header not found");

        std::string chunkExtraDataDelimiterStr = ";";
        auto chunkLength = std::search(data.begin() + (long long) pos, chunkHeaderEnd,
                                       chunkExtraDataDelimiterStr.begin(), chunkExtraDataDelimiterStr.end());
        std::string chunkSizeStr(reinterpret_cast<const char*>(data.data() + pos),
                                 chunkLength - (data.begin() + (long long) pos));

        if (!std::all_of(chunkSizeStr.begin(), chunkSizeStr.end(), [](char c) { return std::isxdigit(c) != 0; })) {
            throw MalformedException("Chunk length is not a number");
        }

        size_t chunkSize;
        try {
            chunkSize = std::stoul(chunkSizeStr, nullptr, 16);
        } catch (std::out_of_range&) {
            throw MalformedException("Chunk length too big");
        }

        if (chunkSize == 0) {
            // Read possible trailers
            std::string finalDelimiterStr = "\r\n\r\n";
            auto trailerEnd = std::search(chunkHeaderEnd + 2, end,
                                          finalDelimiterStr.begin(), finalDelimiterStr.end());

            if (trailerEnd != end) {
                size_t chunkHeaderEndPos = chunkHeaderEnd - data.begin() + 2;

                std::string_view trailerStr(reinterpret_cast<const char*>(data.data() + chunkHeaderEndPos),
                                            trailerEnd - (chunkHeaderEnd + 2));

                while (!trailerStr.empty()) {
                    std::string_view trailer = trailerStr.substr(0, trailerStr.find("\r\n"));
                    if (trailer.empty()) break;

                    // Check for non-ascii characters
                    if (!std::all_of(trailer.begin(), trailer.end(), [](unsigned char c) { return c < 128; })) {
                        throw MalformedException("Non-ascii characters in trailer");
                    }

                    parseHeader(trailer, headers, true);

                    trailerStr.remove_prefix(trailer.size() + 2);
                }
            }

            break;
        } else {
            auto chunkEnd = std::search(chunkHeaderEnd + 2 + (long long) chunkSize, end,
                                        delimiterStr.begin(), delimiterStr.end());

            if (chunkEnd == end) throw MalformedException("Chunk end not found");
            if (chunkEnd != chunkHeaderEnd + 2 + (long long) chunkSize) throw MalformedException("Chunk size mismatch");

            body.insert(body.end(), chunkHeaderEnd + 2, chunkEnd);
        }

        pos += chunkHeaderEnd - (data.begin() + (long long) pos) + 2 + chunkSize + 2;
    }
}

std::string getFinalTransferEncoding(const std::string& transferEncoding) {
    std::string finalTransferEncoding = transferEncoding.substr(
            transferEncoding.find_last_of(',') + 1);

    if (finalTransferEncoding[0] == ' ') finalTransferEncoding = finalTransferEncoding.substr(1);

    return finalTransferEncoding;
}

bool isHTTPBodyComplete(const std::vector<unsigned char>& data, size_t& length, size_t headerLength,
                        std::map<std::string, std::vector<std::string>>& headers,
                        bool isResponse, int status, bool reqWasHead, bool connectionClose) {
    if (isResponse && (reqWasHead || status == 204 || status == 304 || (status >= 100 && status < 200))) {
        length = 0;
        return true;
    }

    if (headers.find("content-length") == headers.end() && headers.find("transfer-encoding") == headers.end()) {
        length = 0;
        return true;
    }

    std::string finalTransferEncoding;
    if (headers.find("transfer-encoding") != headers.end()) {
        finalTransferEncoding = getFinalTransferEncoding(headers["transfer-encoding"][0]);
    }

    if (headers.find("transfer-encoding") != headers.end() && finalTransferEncoding != "chunked") {
        if (!isResponse) throw LengthUnknownException("Length unknown");

        if (connectionClose) {
            length = data.size() - headerLength;
            return true;
        } else {
            return false;
        }
    }

    if (headers.find("transfer-encoding") != headers.end() &&
        headers["transfer-encoding"][0] == "chunked") {
        return isChunkedComplete(data, headerLength, length);
    }

    if (headers.find("content-length") != headers.end() && headers.find("transfer-encoding") == headers.end()) {
        std::string content_length = headers["content-length"][0];
        if (!std::all_of(content_length.begin(), content_length.end(), [](char c) { return std::isdigit(c) != 0; })) {
            throw MalformedException("Content length is not a number");
        }

        try {
            size_t contentLength = std::stoul(content_length);
            if (data.size() >= headerLength + contentLength) {
                length = contentLength;
                return true;
            }
        } catch (std::out_of_range&) {
            throw MalformedException("Content length too big");
        }
    }

    return false;
}

std::string percentDecode(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '%') {
            if (i + 2 >= str.size()) throw MalformedException("Invalid percent encoding");
            char c;
            try {
                c = (char) std::stoul(str.substr(i + 1, 2), nullptr, 16);
            } catch (std::invalid_argument&) {
                throw MalformedException("Invalid percent encoding");
            } catch (std::out_of_range&) {
                throw MalformedException("Invalid percent encoding");
            }

            // Don't allow control characters or DEL
            if (c < 0x20 || c == 0x7F) throw MalformedException("Invalid percent encoding");

            result += c;
            i += 2;
        } else {
            result += str[i];
        }
    }

    return std::move(result);
}

std::string percentEncode(const std::string& str, bool allowSlash) {
    std::string reserved = " !\"#$%&'()*+,/:;=?@[]";

    std::string result;
    result.reserve(str.size());

    for (char i : str) {
        if (i == '/' && allowSlash) {
            result += i;
            continue;
        }

        // Check if character is in the reserved set
        if (reserved.find(i) != std::string::npos || i > 0x7F) {
            result += '%';
            std::stringstream ss;
            ss << std::hex << (unsigned int) i;
            result += ss.str();
        } else {
            result += i;
        }
    }

    return std::move(result);
}

} // namespace http