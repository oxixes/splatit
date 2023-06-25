#include "request.hpp"

namespace http {

bool Request::isHeaderComplete(const std::vector<unsigned char>& data, size_t& length) {
    std::string_view dataView(reinterpret_cast<const char*>(data.data()), data.size());
    size_t pos = dataView.find("\r\n\r\n");
    if (pos == std::string_view::npos) return false;

    length = pos + 4;
    return true;
}

void Request::parseHTTPHeader(const std::vector<unsigned char>& data) {
    std::string_view dataView(reinterpret_cast<const char*>(data.data()), data.size());

    // Check that only ascii characters are used
    for (char i : dataView) {
        if (i > 127) throw MalformedException("Header contains non-ascii characters");
    }

    size_t firstLineEnd = dataView.find("\r\n");
    std::string_view firstLine = dataView.substr(0, firstLineEnd);

    std::string_view methodStr = firstLine.substr(0, firstLine.find(' '));
    if (methodStr == "GET") {
        method = Method::GET;
    } else if (methodStr == "POST") {
        method = Method::POST;
    } else if (methodStr == "PUT") {
        method = Method::PUT;
    } else if (methodStr == "DELETE") {
        method = Method::DELETE;
    } else if (methodStr == "HEAD") {
        method = Method::HEAD;
    } else {
        throw MethodNotSupportedException("Method not supported");
    }

    std::string_view pathStr = firstLine.substr(firstLine.find(' ') + 1);
    path = std::string(pathStr.substr(0, pathStr.find('?')));
    if (path.back() == '/') path.pop_back(); // Remove possible trailing slash

    if (pathStr.find('?') != std::string_view::npos) {
        std::string_view queryStr = pathStr.substr(pathStr.find('?') + 1);
        while (!queryStr.empty()) {
            std::string_view param = queryStr.substr(0, queryStr.find('&'));
            std::string_view key = param.substr(0, param.find('='));
            std::string_view value;

            if (param.find('=') != std::string_view::npos) {
                value = param.substr(param.find('=') + 1);
            } else {
                value = "";
            }

            query.insert({std::string(key), std::string(value)});

            if (queryStr.find('&') == std::string_view::npos) break;
            queryStr.remove_prefix(param.size() + 1);
        }
    }

    std::string_view versionStr = firstLine.substr(firstLine.find(' ', pathStr.size()) + 1);
    if (versionStr == "HTTP/1.0") {
        version = Version::HTTP_1_0;
    } else if (versionStr == "HTTP/1.1") {
        version = Version::HTTP_1_1;
    } else {
        throw VersionNotSupportedException("Version not supported");
    }

    std::string_view headerStr = dataView.substr(firstLineEnd + 2, dataView.find("\r\n\r\n") - firstLineEnd - 4);
    while (!headerStr.empty()) {
        std::string_view header = headerStr.substr(0, headerStr.find("\r\n"));
        if (header.empty()) break;

        parseHeader(header, headers);

        headerStr.remove_prefix(header.size() + 2);
    }
}

bool Request::isBodyComplete(const std::vector<unsigned char>& data, size_t& length, size_t headerLength,
                             bool& unknownLength) {
    unknownLength = false;
    if (headers.find("content-length") == headers.end() && (headers.find("transfer-encoding") == headers.end() ||
                                                            headers["transfer-encoding"][0] != "chunked")) {
        unknownLength = true;
        return false;
    }

    if (headers.find("transfer-encoding") != headers.end() &&
        headers["transfer-encoding"][0] == "chunked") {
        std::string_view dataView(reinterpret_cast<const char*>(data.data() + headerLength), data.size() - headerLength);
        while (true) {
            std::string_view chunkHeaderStr = dataView.substr(0, dataView.find("\r\n"));
            if (chunkHeaderStr.empty()) return false;
            std::string_view chunkSizeStr = chunkHeaderStr.substr(0, chunkHeaderStr.find(';'));

            size_t chunkSize = std::stoul(std::string(chunkSizeStr), nullptr, 16);

        }
    }

    return true;
}

} // namespace http