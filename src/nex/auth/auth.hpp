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

    async::Task<std::optional<std::string>> getOrRegisterUserPassword(uint32_t pid) const;

private:
    async::Task<void> login(ClientInfo client, Request req, std::unique_ptr<String> username);
    async::Task<void> loginEx(ClientInfo client, Request req, std::unique_ptr<String> username, std::unique_ptr<AnyDataHolder> authInfo);
    async::Task<void> requestTicket(ClientInfo client, Request req, std::unique_ptr<PID> idSource, std::unique_ptr<PID> idTarget);

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
