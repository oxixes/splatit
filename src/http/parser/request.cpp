#include "request.hpp"

namespace http {

Request::Request(const std::string& path, http::Method method, http::Version version) {
    this->path = path;
    this->method = method;
    this->version = version;
}

Method Request::getMethod() const {
    return method;
}

Version Request::getVersion() const {
    return version;
}

std::string Request::getPath() const {
    return path;
}

bool Request::hasQuery(const std::string &key) const {
    return query.find(key) != query.end();
}

std::string Request::getQuery(const std::string &key) const {
    return query.at(key);
}

void Request::setQuery(const std::string &key, const std::string &value) {
    query[key] = value;
}

bool Request::hasHeader(const std::string &key) const {
    return headers.find(key) != headers.end();
}

const std::vector<std::string>& Request::getHeader(const std::string& key) const {
    return headers.at(key);
}

void Request::setHeader(const std::string& key, const std::string& value) {
    headers[key] = std::vector<std::string>({value});
}

void Request::addHeader(const std::string& key, const std::string& value) {
    if (headers.find(key) == headers.end()) {
        headers[key] = std::vector<std::string>({value});
    } else {
        headers[key].push_back(value);
    }
}

const std::vector<uint8_t>& Request::getBody() const {
    return body;
}

void Request::setBody(std::vector<uint8_t>& newBody) {
    body = std::move(newBody);
}

Request Request::parse(const std::vector<uint8_t>& data, size_t& length) {
    length = 0;
    Request request;

    if (!isHeaderComplete(data, length)) {
        throw NotCompleteException("Header incomplete");
    }

    request.parseHTTPHeader(data, length);

    size_t contentLength = 0;
    if (!request.isBodyComplete(data, contentLength, length)) {
        throw NotCompleteException("Body incomplete");
    }

    request.parseHTTPBody(data, length, contentLength);

    length += contentLength;

    return std::move(request);
}

bool Request::isHeaderComplete(const std::vector<uint8_t>& data, size_t& length) {
    return isHTTPHeaderComplete(data, length);
}

// It is assumed that the header is complete, as isHeaderComplete() should be called before this
void Request::parseHTTPHeader(const std::vector<uint8_t>& data, size_t length) {
    std::string_view dataView(reinterpret_cast<const char*>(data.data()), length);

    // Check that only valid ascii characters are used
    for (char i : dataView) {
        if ((i < 0x20 || i >= 0x7F) && i != 0x0A && i != 0x0D)
            throw MalformedException("Header contains invalid characters");
    }

    size_t firstLineEnd = dataView.find("\r\n");
    std::string_view firstLine = dataView.substr(0, firstLineEnd);

    std::string_view methodStr = firstLine.substr(0, firstLine.find(' '));
    if (methodStr == "GET") {
        method = Method::M_GET;
    } else if (methodStr == "POST") {
        method = Method::M_POST;
    } else if (methodStr == "PUT") {
        method = Method::M_PUT;
    } else if (methodStr == "DELETE") {
        method = Method::M_DELETE;
    } else if (methodStr == "HEAD") {
        method = Method::M_HEAD;
    } else {
        throw MethodNotSupportedException("Method not supported");
    }

    std::string_view firstLineParams = firstLine.substr(firstLine.find(' ') + 1);
    std::string_view pathStr = firstLineParams.substr(0, firstLineParams.find(' '));
    std::string_view pathNoFragment = pathStr.substr(0, pathStr.find('#'));

    // This will parse %2F as /, which means that it will be interpreted as a path separator
    // This is intended.
    path = percentDecode(std::string(pathNoFragment.substr(0, pathStr.find('?'))));

    if (path.empty()) throw MalformedException("Path is empty");
    if (path[0] != '/') throw MalformedException("Path does not start with '/'");
    if (path.size() != 1 && path.back() == '/') path.pop_back(); // Remove possible trailing slash

    if (pathNoFragment.find('?') != std::string_view::npos) {
        std::string_view queryStr = pathNoFragment.substr(pathNoFragment.find('?') + 1);
        parseQuery(queryStr, query);
    }

    std::string_view versionStr = firstLineParams.substr(firstLineParams.find(' ') + 1);

    if (versionStr == "HTTP/1.0") {
        version = Version::HTTP_1_0;
    } else if (versionStr == "HTTP/1.1") {
        version = Version::HTTP_1_1;
    } else {
        throw VersionNotSupportedException("Version not supported");
    }

    std::string_view headerStr = dataView.substr(firstLineEnd + 2, dataView.find("\r\n\r\n") - firstLineEnd);
    while (!headerStr.empty()) {
        std::string_view header = headerStr.substr(0, headerStr.find("\r\n"));
        if (header.empty()) break;

        parseHeader(header, headers);

        headerStr.remove_prefix(header.size() + 2);
    }
}

// It is assumed that the header is valid and has been parsed, as parseHTTPHeader() should be called before this.
bool Request::isBodyComplete(const std::vector<uint8_t>& data, size_t& length, size_t headerLength) {
    return isHTTPBodyComplete(data, length, headerLength, headers);
}

// It is assumed that the body is complete, and the header is parsed, as this function is only called after isBodyComplete.
// Also, the length is known.
void Request::parseHTTPBody(const std::vector<uint8_t>& data, size_t headerLength, size_t length) {
    std::string finalTransferEncoding;
    if (headers.find("transfer-encoding") != headers.end()) {
        finalTransferEncoding = getFinalTransferEncoding(headers["transfer-encoding"][0]);
    }

    if (headers.find("transfer-encoding") != headers.end() && finalTransferEncoding == "chunked") {
        parseChunked(data, headers, length, headerLength, body);
    } else if (headers.find("content-length") != headers.end() && headers.find("transfer-encoding") == headers.end()) {
        body.insert(body.end(), data.begin() + (long long) headerLength, data.begin()
                        + (long long) headerLength + (long long) length);
    }
}

std::vector<uint8_t> Request::serialize() const {
    std::vector<uint8_t> data;

    std::string delimiter = "\r\n";

    std::string methodStr;
    switch (method) {
        case Method::M_GET:
            methodStr = "GET";
            break;
        case Method::M_POST:
            methodStr = "POST";
            break;
        case Method::M_PUT:
            methodStr = "PUT";
            break;
        case Method::M_DELETE:
            methodStr = "DELETE";
            break;
        case Method::M_HEAD:
            methodStr = "HEAD";
            break;
    }

    std::string versionStr;
    switch (version) {
        case Version::HTTP_1_0:
            versionStr = "HTTP/1.0";
            break;
        case Version::HTTP_1_1:
            versionStr = "HTTP/1.1";
            break;
    }

    data.insert(data.end(), methodStr.begin(), methodStr.end());
    data.push_back(' ');

    std::string pathStr = percentEncode(path, true);

    bool first = true;
    for (auto& queryParam : query) {
        if (first) {
            pathStr.push_back('?');
            first = false;
        } else {
            pathStr.push_back('&');
        }

        std::string queryParamStr = percentEncode(queryParam.first);
        pathStr.insert(pathStr.end(), queryParamStr.begin(), queryParamStr.end());
        pathStr.push_back('=');
        queryParamStr = percentEncode(queryParam.second);
        pathStr.insert(pathStr.end(), queryParamStr.begin(), queryParamStr.end());
    }

    data.insert(data.end(), pathStr.begin(), pathStr.end());

    data.push_back(' ');
    data.insert(data.end(), versionStr.begin(), versionStr.end());
    data.insert(data.end(), delimiter.begin(), delimiter.end());

    for (auto& header : headers) {
        if (header.first == "content-length") continue;

        // We won't support chunked encoding for now
        if (header.first == "transfer-encoding") {
            std::string finalTransferEncoding = getFinalTransferEncoding(header.second[0]);

            if (finalTransferEncoding == "chunked") continue;
        }

        for (auto& value : header.second) {

            data.insert(data.end(), header.first.begin(), header.first.end());
            data.push_back(':');
            data.push_back(' ');
            data.insert(data.end(), value.begin(), value.end());
            data.insert(data.end(), delimiter.begin(), delimiter.end());
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