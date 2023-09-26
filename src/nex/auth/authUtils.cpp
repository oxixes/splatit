#include <nlohmann/json.hpp>
#include "authUtils.hpp"
#include "../../crypto/tools.hpp"

namespace nex::rmc::utils {

    bool checkJWT(const std::string& jwtToken, const std::string& base64JWTKey, const std::string& serverId, ClientInfo& client,
                  const std::shared_ptr<Logger::Logger>& logger, Logger::group logGroup) {
        if (!crypto::verifyJWT(base64JWTKey, jwtToken)) {
            logger->log(Logger::level::WARN, logGroup, "Invalid JWT token from " + util::ipv4ToString(client.address.address)
                                                       + ":" + std::to_string(client.address.address.port));
            return false;
        }

        std::string jwtData = ((std::string) jwtToken).substr(((std::string) jwtToken).find('.') + 1);
        jwtData = jwtData.substr(0, jwtData.find('.'));

        auto jwtJson = nlohmann::json::parse(crypto::base64UrlDecode(jwtData));
        if (time(nullptr) > jwtJson["exp"].get<time_t>()) {
            logger->log(Logger::level::WARN, logGroup, "Expired JWT token from " + util::ipv4ToString(client.address.address)
                                                       + ":" + std::to_string(client.address.address.port));
            return false;
        }

        if (jwtJson["iss"].get<std::string>() != "account") {
            logger->log(Logger::level::WARN, logGroup, "Invalid JWT issuer from " + util::ipv4ToString(client.address.address)
                                                       + ":" + std::to_string(client.address.address.port));
            return false;
        }

        if (jwtJson["game_server_id"].get<std::string>() != serverId) {
            logger->log(Logger::level::WARN, logGroup, "Invalid JWT server id from " + util::ipv4ToString(client.address.address)
                                                       + ":" + std::to_string(client.address.address.port));
            return false;
        }

        if (jwtJson["sub"].get<uint32_t>() != client.pid) {
            logger->log(Logger::level::WARN, logGroup, "Non-matching PID in JWT from " + util::ipv4ToString(client.address.address)
                                                       + ":" + std::to_string(client.address.address.port));
            return false;
        }

        return true;
    }

} // namespace nex::rmc::utils