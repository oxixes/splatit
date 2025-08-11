#include <nlohmann/json.hpp>
#include <random>

#include "authUtils.hpp"
#include "../../crypto/tools.hpp"

namespace nex::rmc::utils {

    bool checkJWT(const std::string& jwtToken, const std::string& base64JWTKey, const std::string& serverId, const ClientInfo& client,
                  const std::shared_ptr<Logger::Logger>& logger, Logger::group logGroup, std::string& usernameOut) {
        if (!crypto::verifyJWT(base64JWTKey, jwtToken)) {
            logger->log(Logger::level::WARN, logGroup, "Invalid JWT token from " + util::ipv4ToString(client.address.address)
                                                       + ":" + std::to_string(client.address.address.port));
            return false;
        }

        std::string jwtData = jwtToken.substr(jwtToken.find('.') + 1);
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

        usernameOut = jwtJson["username"].get<std::string>();
        return true;
    }

    std::string generateUserPassword() {
        // Generate a 16 character alphanumeric password
        const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

        std::string password;
        password.reserve(16);

        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<> dist(0, static_cast<int>(charset.size()) - 1);

        for (size_t i = 0; i < 16; ++i) {
            password += charset[dist(rng)];
        }

        return std::move(password);
    }

} // namespace nex::rmc::utils