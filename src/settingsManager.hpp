#ifndef SPLATOON_SERVER_SETTINGSMANAGER_HPP
#define SPLATOON_SERVER_SETTINGSMANAGER_HPP

#include <string>
#include <fstream>
#include <sys/stat.h>
#include <filesystem>
#include <set>

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include "argParser.hpp"
#include "logger.hpp"
#include "socket/socket.hpp"

using json = nlohmann::json;
using json_validator = nlohmann::json_schema::json_validator;
namespace fs = std::filesystem;

class SettingsManager {
public:
    explicit SettingsManager(std::shared_ptr<Logger::Logger> logger);
    ~SettingsManager() = default;

    bool init(const argParser::options& serverOptions);

    [[nodiscard]] bool isAccountEnabled() const;
    [[nodiscard]] bool allowRealWiiU() const;
    [[nodiscard]] bool allowGeneratedWiiU() const;
    [[nodiscard]] bool isBOSSEnabled() const;
    [[nodiscard]] bool isFriendsAuthEnabled() const;
    [[nodiscard]] bool isFriendsSecureEnabled() const;
    [[nodiscard]] bool isSplatoonAuthEnabled() const;
    [[nodiscard]] bool isSplatoonSecureEnabled() const;
    [[nodiscard]] bool isgRPCEnabled() const;

    [[nodiscard]] fs::path getSSLCertPath() const;
    [[nodiscard]] fs::path getSSLCACertPath() const;
    [[nodiscard]] fs::path getSSLKeyPath() const;
    [[nodiscard]] fs::path getSSLCAKeyPath() const;
    [[nodiscard]] fs::path getDeviceKeyPath() const;
    [[nodiscard]] std::string getTopDomain() const;
    [[nodiscard]] fs::path getBOSSPath() const;
    [[nodiscard]] fs::path getMiiImagesPath() const;

    [[nodiscard]] json getDBSettings() const;

    [[nodiscard]] sock::IPv4Addr getHTTPListenAddress() const;
    [[nodiscard]] int getHTTPWorkerCount() const;
    [[nodiscard]] int getHTTPKeepAliveTimeout() const;
    [[nodiscard]] bool isHTTP_SSL_Enabled() const;

    [[nodiscard]] std::set<sock::IPv4Addr> getKnownProxies() const;

    [[nodiscard]] sock::IPv4Addr getFriendsAuthListenAddress() const;
    [[nodiscard]] int getFriendsAuthWorkerCount() const;
    [[nodiscard]] sock::IPv4Addr getFriendsSecureServerAddress() const;

    [[nodiscard]] sock::IPv4Addr getFriendsSecureListenAddress() const;
    [[nodiscard]] int getFriendsSecureWorkerCount() const;

    [[nodiscard]] sock::IPv4Addr getSplatoonAuthListenAddress() const;
    [[nodiscard]] int getSplatoonAuthWorkerCount() const;
    [[nodiscard]] sock::IPv4Addr getSplatoonSecureServerAddress() const;

    [[nodiscard]] sock::IPv4Addr getSplatoonSecureListenAddress() const;
    [[nodiscard]] int getSplatoonSecureWorkerCount() const;

    [[nodiscard]] sock::IPv4Addr getgRPCListenAddress() const;

    [[nodiscard]] std::vector<std::string> getDomains() const;

    // These functions return the base64 encoded key
    [[nodiscard]] std::string getTokenKey() const;
    [[nodiscard]] std::string getRefreshTokenKey() const;
    [[nodiscard]] std::string getNEXTokenKey() const;

    [[nodiscard]] std::string getGameServerHost(const std::string& id) const;

private:
    json settings;
    std::shared_ptr<Logger::Logger> logger;

    struct domains {
        std::string account;
        std::string miiSecure;
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
        bool gRPC = false;
    } enabledServers;

    bool openOrCreateFiles(const argParser::options& serverOptions, std::ifstream& settingsFileHandler,
                           std::ifstream& schemaFileHandler);
    bool validateSettings(const argParser::options& serverOptions);
    bool generateDefaultSettingsJSON(const argParser::options& serverOptions);
};

#endif //SPLATOON_SERVER_SETTINGSMANAGER_HPP
