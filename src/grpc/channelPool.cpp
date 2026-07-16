#include "channelPool.hpp"

#include <fstream>
#include <sstream>

namespace grpcimpl {

static std::string readFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::shared_ptr<grpc::ChannelCredentials> createTlsChannelCredentials(
        const fs::path& certPath, const fs::path& keyPath, const fs::path& caCertPath) {
    grpc::SslCredentialsOptions options;
    options.pem_cert_chain = readFile(certPath);
    options.pem_private_key = readFile(keyPath);
    options.pem_root_certs = readFile(caCertPath);
    return grpc::SslCredentials(options);
}

std::shared_ptr<grpc::Channel> ChannelPool::getChannel(const std::string& address) {
    std::unique_lock lock(mutex);

    auto it = listMap.find(address);
    if (it != listMap.end()) {
        // Channel already exists, move it to the front of the LRU list
        lruList.splice(lruList.begin(), lruList, it->second.second);
        return it->second.first;
    }

    // Create a new channel
    auto channel = grpc::CreateChannel(address, credentials ? credentials : grpc::InsecureChannelCredentials());

    if (lruList.size() >= maxSize) {
        // Remove the least recently used channel
        const auto& lruAddress = lruList.back();
        listMap.erase(lruAddress);
        lruList.pop_back();
    }

    // Add the new channel to the front of the LRU list
    lruList.push_front(address);
    listMap[address] = {channel, lruList.begin()};

    return channel;
}

} // namespace grpcimpl