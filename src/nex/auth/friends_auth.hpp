#ifndef SPLATOON_SERVER_FRIENDS_AUTH_HPP
#define SPLATOON_SERVER_FRIENDS_AUTH_HPP

#include "../rmc/server.hpp"
#include "../types/common/ints.hpp"
#include "../types/common/string.hpp"

namespace nex::rmc {

class FriendsAuthRMC : public Server {
public:
    explicit FriendsAuthRMC(std::shared_ptr<Logger::Logger> logger);

    void login(ClientInfo client, uint32_t callId, String test);
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDS_AUTH_HPP
