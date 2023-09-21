#ifndef SPLATOON_SERVER_FRIENDSAUTH_HPP
#define SPLATOON_SERVER_FRIENDSAUTH_HPP

#include "../rmc/server.hpp"
#include "../types/common/ints.hpp"
#include "../types/common/string.hpp"
#include "../../db/database.hpp"

namespace nex::rmc {

class FriendsAuthRMC : public Server {
public:
    explicit FriendsAuthRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                            sock::IPv4Addr secureAddr);
    ~FriendsAuthRMC() override = default;

private:
    void login(ClientInfo client, Request req, String username);
    void requestTicket(ClientInfo client, Request req, PID idSource, PID idTarget);

    std::string getUserAccessPassword(uint32_t pid);

    const inline static std::string BUILD = "branch:origin/feature/45925_FixAutoReconnect build:3_10_11_2006_0";

    std::shared_ptr<db::Database> db;
    sock::IPv4Addr secureAddr;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDSAUTH_HPP
