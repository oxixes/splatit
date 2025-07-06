#include "channelPool.hpp"

namespace grpcimpl {

std::shared_ptr<grpc::Channel> ChannelPool::getChannel(const std::string& address) {
    std::unique_lock lock(mutex);

    auto it = listMap.find(address);
    if (it != listMap.end()) {
        // Channel already exists, move it to the front of the LRU list
        lruList.splice(lruList.begin(), lruList, it->second.second);
        return it->second.first;
    }

    // Create a new channel
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());

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