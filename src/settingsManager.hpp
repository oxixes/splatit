#ifndef SPLATOON_SERVER_SETTINGSMANAGER_HPP
#define SPLATOON_SERVER_SETTINGSMANAGER_HPP

#include <string>
#include <fstream>
#include <sys/stat.h>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include "argParser.hpp"
#include "logger.hpp"

using json = nlohmann::json;
using json_validator = nlohmann::json_schema::json_validator;
namespace fs = std::filesystem;

class SettingsManager {
public:
    explicit SettingsManager(std::shared_ptr<Logger::Logger> logger);
    ~SettingsManager() = default;

    bool init(const argParser::options& serverOptions);

    [[nodiscard]] bool isAccountEnabled() const;
    [[nodiscard]] bool isBOSSEnabled() const;
    [[nodiscard]] bool isFriendsAuthEnabled() const;
    [[nodiscard]] bool isFriendsSecureEnabled() const;
    [[nodiscard]] bool isSplatoonAuthEnabled() const;
    [[nodiscard]] bool isSplatoonSecureEnabled() const;

    [[nodiscard]] fs::path getSSLCertPath() const;
    [[nodiscard]] fs::path getSSLCACertPath() const;
    [[nodiscard]] fs::path getSSLKeyPath() const;
    [[nodiscard]] fs::path getSSLCAKeyPath() const;
    [[nodiscard]] fs::path getTopDomain() const;
    [[nodiscard]] fs::path getBOSSPath() const;

    [[nodiscard]] json getDBSettings() const;

    [[nodiscard]] std::vector<std::string> getDomains() const;
private:
    json settings;
    std::shared_ptr<Logger::Logger> logger;

    struct domains {
        std::string account;
        std::string bossNPTS;
        std::string bossNPPL;
        std::string bossNPDI;
    } domains;

    struct enabledServers {
        bool account = false;
        bool boss = false;
        bool friendsAuth = false;
        bool friendsSecure = false;
        bool splatoonAuth = false;
        bool splatoonSecure = false;
    } enabledServers;

    bool openOrCreateFiles(const argParser::options& serverOptions, std::ifstream& settingsFileHandler,
                           std::ifstream& schemaFileHandler);
    bool validateSettings(const argParser::options& serverOptions);
    bool generateDefaultSettingsJSON(const argParser::options& serverOptions);
};

#endif //SPLATOON_SERVER_SETTINGSMANAGER_HPP
