#include "friends_auth.hpp"

namespace nex::rmc {

FriendsAuthRMC::FriendsAuthRMC(std::shared_ptr<Logger::Logger> logger) : Server(std::move(logger)) {
    logGroup = Logger::group::FRIENDS_AUTH;
}

} // namespace nex::rmc