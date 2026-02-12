#include "sharedState.hpp"

namespace ss {

SharedState::SharedState(std::shared_ptr<Logger::Logger> logger, const SSType type, const uint32_t serverId,
    const std::string& publicFacingRPCAddress) {
    this->logger = std::move(logger);
    this->type = type;
    this->serverId = serverId;
    this->publicFacingRPCAddress = publicFacingRPCAddress;
}

} // namespace ss