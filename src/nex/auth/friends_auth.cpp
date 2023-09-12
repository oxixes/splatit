#include "friends_auth.hpp"

#include "../types/common/string.hpp"

namespace nex::rmc {

FriendsAuthRMC::FriendsAuthRMC(std::shared_ptr<Logger::Logger> logger) : Server(std::move(logger)) {
    logGroup = Logger::group::FRIENDS_AUTH;

    registerCall(this, &FriendsAuthRMC::login, 10, 1);
}

void FriendsAuthRMC::login(nex::rmc::ClientInfo client, uint32_t callId, String username) {
    logger->log(Logger::level::INFO, logGroup, username);
}

} // namespace nex::rmc