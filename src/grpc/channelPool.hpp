#ifndef SPLATOON_SERVER_CHANNELPOOL_HPP
#define SPLATOON_SERVER_CHANNELPOOL_HPP

#include <grpcpp/grpcpp.h>
#include <list>

namespace grpcimpl {

class ChannelPool {
public:
    explicit ChannelPool(size_t maxSize) : maxSize(maxSize) {}

    std::shared_ptr<grpc::Channel> getChannel(const std::string& address);

private:
    size_t maxSize;
    std::list<std::string> lruList;
    std::unordered_map<std::string, std::pair<std::shared_ptr<grpc::Channel>, std::list<std::string>::iterator>> listMap;
    std::mutex mutex;
};

} // namespace grpcimpl

#endif //SPLATOON_SERVER_CHANNELPOOL_HPP
