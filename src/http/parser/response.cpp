#include <iostream>
#include "response.hpp"

namespace http {

Response::Response(http::Version version, int status) {
    this->version = version;
    this->status = status;
}

Version Response::getVersion() const {
    return version;
}

int Response::getStatus() const {
    return status;
}

bool Response::hasHeader(const std::string &key) const {
    return headers.find(key) != headers.end();
}

const std::vector<std::string>& Response::getHeader(const std::string& key) const {
    return headers.at(key);
}

void Response::setHeader(const std::string& key, const std::string& value) {
    headers[key] = std::vector<std::string>({value});
}

void Response::addHeader(const std::string& key, const std::string& value) {
    if (headers.find(key) == headers.end()) {
        headers[key] = std::vector<std::string>({value});
    } else {
        headers[key].push_back(value);
    }
}

const std::vector<uint8_t>& Response::getBody() const {
    return body;
}

void Response::setBody(std::vector<uint8_t>& newBody) {
    this->body = std::move(newBody);
}

Response Response::parse(const std::vector<uint8_t>& data, size_t& length, bool connectionClose, bool reqWasHead) {
    length = 0;
    Response response;

    if (!isHeaderComplete(data, length)) {
        throw NotCompleteException("Header incomplete");
    }

    response.parseHTTPHeader(data, length);

    size_t contentLength = 0;
    if (!response.isBodyComplete(data, contentLength, length, connectionClose, reqWasHead)) {
        throw NotCompleteException("Body incomplete");
    }

    response.parseHTTPBody(data, length, contentLength);

    length += contentLength;

    return std::move(response);
}

bool Response::isHeaderComplete(const std::vector<uint8_t>& data, size_t& length) {
    return isHTTPHeaderComplete(data, length);
}

void Response::parseHTTPHeader(const std::vector<uint8_t>& data, size_t length) {
    std::string_view headerStr(reinterpret_cast<const char*>(data.data()), length);

    std::string_view statusLine = headerStr.substr(0, headerStr.find("\r\n"));
    std::string_view versionStr = statusLine.substr(0, statusLine.find(' '));

    if (versionStr == "HTTP/1.1") {
        version = Version::HTTP_1_1;
    } else if (versionStr == "HTTP/1.0") {
        version = Version::HTTP_1_0;
    } else {
        throw VersionNotSupportedException("Version not supported");
    }

    std::string_view statusStr = statusLine.substr(statusLine.find(' ') + 1);
    try {
        status = std::stoi(std::string(statusStr.substr(0, statusStr.find(' '))));
    } catch (std::invalid_argument& e) {
        throw MalformedException("Status code is not a number");
    }

    if (statusStr.find(' ') == std::string_view::npos) {
        throw MalformedException("Status code is not followed by a reason phrase");
    }

    if (STATUS_CODE_MSG.find(status) == STATUS_CODE_MSG.end()) {
        throw MalformedException("Status code is not valid");
    }

    std::string_view headersStr = headerStr.substr(headerStr.find("\r\n") + 2);
    while (!headersStr.empty()) {
        std::string_view header = headersStr.substr(0, headersStr.find("\r\n"));
        if (header.empty()) break;

        parseHeader(header, headers);

        headersStr.remove_prefix(header.size() + 2);
    }
}

bool Response::isBodyComplete(const std::vector<uint8_t>& data, size_t& length, size_t headerLength,
                              bool connectionClose, bool reqWasHead) {
    return isHTTPBodyComplete(data, length, headerLength, headers, true, status, reqWasHead, connectionClose);
}

void Response::parseHTTPBody(const std::vector<uint8_t>& data, size_t headerLength, size_t length) {
    std::string finalTransferEncoding;
    if (headers.find("transfer-encoding") != headers.end()) {
        finalTransferEncoding = getFinalTransferEncoding(headers["transfer-encoding"][0]);
    }

    if (headers.find("transfer-encoding") != headers.end() && finalTransferEncoding == "chunked") {
        parseChunked(data, headers, length, headerLength, body);
    } else {
        body.insert(body.end(), data.begin() + (long long) headerLength, data.begin()
            + (long long) headerLength + (long long) length);
    }
}

std::vector<uint8_t> Response::serialize() const {
    std::vector<uint8_t> data;

    std::string delimiter = "\r\n";

    std::string statusLine;
    if (version == Version::HTTP_1_1) {
        statusLine = "HTTP/1.1 ";
    } else if (version == Version::HTTP_1_0) {
        statusLine = "HTTP/1.0 ";
    }

    statusLine += std::to_string(status) + " " + STATUS_CODE_MSG.at(status) + "\r\n";
    data.insert(data.end(), statusLine.begin(), statusLine.end());

    for (auto& header : headers) {
        if (header.first == "content-length") continue;

        // We won't support chunked encoding for now
        if (header.first == "transfer-encoding") {
            std::string finalTransferEncoding = getFinalTransferEncoding(header.second[0]);

            if (finalTransferEncoding == "chunked") continue;
        }

        for (auto& value : header.second) {
            std::string headerStr = header.first + ": " + value + "\r\n";
            data.insert(data.end(), headerStr.begin(), headerStr.end());
        }
    }

    // Add content length
    std::string contentLengthHeader = "Content-Length: " + std::to_string(body.size()) + "\r\n";
    data.insert(data.end(), contentLengthHeader.begin(), contentLengthHeader.end());
    data.insert(data.end(), delimiter.begin(), delimiter.end());

    data.insert(data.end(), body.begin(), body.end());

    return std::move(data);
}

} // namespace http