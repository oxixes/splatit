#ifndef SPLATOON_SERVER_CHANNELPOOL_HPP
#define SPLATOON_SERVER_CHANNELPOOL_HPP

#include <grpcpp/grpcpp.h>
#include <list>
#include <filesystem>

namespace fs = std::filesystem;

namespace grpcimpl {

std::shared_ptr<grpc::ChannelCredentials> createTlsChannelCredentials(
    const fs::path& certPath, const fs::path& keyPath, const fs::path& caCertPath);

class ChannelPool {
public:
    explicit ChannelPool(size_t maxSize) : maxSize(maxSize) {}
    ChannelPool(size_t maxSize, std::shared_ptr<grpc::ChannelCredentials> credentials)
        : maxSize(maxSize), credentials(std::move(credentials)) {}

    std::shared_ptr<grpc::Channel> getChannel(const std::string& address);

private:
    size_t maxSize;
    std::shared_ptr<grpc::ChannelCredentials> credentials;
    std::list<std::string> lruList;
    std::unordered_map<std::string, std::pair<std::shared_ptr<grpc::Channel>, std::list<std::string>::iterator>> listMap;
    std::mutex mutex;
};

} // namespace grpcimpl

#endif //SPLATOON_SERVER_CHANNELPOOL_HPP
