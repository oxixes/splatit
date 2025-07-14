#ifndef SPLATOON_SERVER_AUTHUTILS_HPP
#define SPLATOON_SERVER_AUTHUTILS_HPP

#include <string>
#include "../rmc/server.hpp"

namespace nex::rmc::utils {

    bool checkJWT(const std::string& jwtToken, const std::string& base64JWTKey, const std::string& serverId, const ClientInfo& client,
                  const std::shared_ptr<Logger::Logger>& logger, Logger::group logGroup);

    std::string generateUserPassword();

} // namespace nex::rmc::utils

#endif //SPLATOON_SERVER_AUTHUTILS_HPP
