#ifndef SPLATOON_SERVER_FRIENDS_AUTH_HPP
#define SPLATOON_SERVER_FRIENDS_AUTH_HPP

#include "../rmc/server.hpp"

namespace nex::rmc {

class FriendsAuthRMC : public Server {
public:
    explicit FriendsAuthRMC(std::shared_ptr<Logger::Logger> logger);
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDS_AUTH_HPP
