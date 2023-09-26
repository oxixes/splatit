#ifndef SPLATOON_SERVER_AUTH_HPP
#define SPLATOON_SERVER_AUTH_HPP

#include "../rmc/server.hpp"
#include "../types/common/ints.hpp"
#include "../types/common/string.hpp"
#include "../types/common/anyDataHolder.hpp"
#include "../../db/database.hpp"

namespace nex::rmc {

class AuthRMC : public Server {
public:
    explicit AuthRMC(std::shared_ptr<Logger::Logger> logger, Logger::group logGroup, std::shared_ptr<db::Database> db,
                     sock::IPv4Addr secureAddr, std::string serverId, std::vector<uint8_t> secureServerKey,
                     std::string build, std::string base64JWTKey, bool friends);
    ~AuthRMC() override = default;

private:
    void login(ClientInfo client, Request req, String username);
    void loginEx(ClientInfo client, Request req, String username, AnyDataHolder authInfo);
    void requestTicket(ClientInfo client, Request req, PID idSource, PID idTarget);

    std::string getUserAccessPassword(uint32_t pid);

    std::shared_ptr<db::Database> db;
    sock::IPv4Addr secureAddr;

    std::string serverId;
    std::vector<uint8_t> secureServerKey;

    std::string build;

    std::string base64JWTKey;

    bool friends;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_AUTH_HPP
