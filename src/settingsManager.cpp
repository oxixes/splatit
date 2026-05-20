#include "settingsManager.hpp"
#include "crypto/tools.hpp"
#include "util/util.hpp"

#include <utility>

SettingsManager::SettingsManager(std::shared_ptr<Logger::Logger> logger) {
    this->logger = std::move(logger);
}

bool SettingsManager::init(const argParser::options& serverOptions) {
    if(!validateSettings(serverOptions)) {
        return false;
    }

//    if (!serverOptions.no_account) {
//        domains.account = "account." + std::string(settings["domain"]);
//    }

//    if (!serverOptions.no_boss) {
//        domains.bossNPTS = "npts.app." + std::string(settings["domain"]);
//        domains.bossNPPL = "nppl.app." + std::string(settings["domain"]);
//        domains.bossNPDI = "npdi.cdn." + std::string(settings["domain"]);
//    }

    if (settings.contains("accounts") && settings["accounts"]["enabled"]) {
        enabledServers.account = true;
        domains.account = "account." + std::string(settings["domain"]);
        domains.miiSecure = "mii-secure.account." + std::string(settings["domain"]);
    }

    if (settings.contains("boss") && settings["boss"]["enabled"]) {
        enabledServers.boss = true;
        domains.bossNPTS = "npts.app." + std::string(settings["domain"]);
        domains.bossNPPL = "nppl.app." + std::string(settings["domain"]);
        domains.bossNPDI = "npdi.cdn." + std::string(settings["domain"]);
    }

    if (settings.contains("friendsAuth") && settings["friendsAuth"]["enabled"]) {
        enabledServers.friendsAuth = true;
    }

    if (settings.contains("friendsSecure") && settings["friendsSecure"]["enabled"]) {
        enabledServers.friendsSecure = true;
    }

    if (settings.contains("splatoonAuth") && settings["splatoonAuth"]["enabled"]) {
        enabledServers.splatoonAuth = true;
    }

    if (settings.contains("splatoonSecure") && settings["splatoonSecure"]["enabled"]) {
        enabledServers.splatoonSecure = true;
    }

    if (settings.contains("grpc") && settings["grpc"]["enabled"]) {
        enabledServers.gRPC = true;
    }

    return true;
}

bool SettingsManager::openOrCreateFiles(const argParser::options &serverOptions, std::ifstream& settingsFileHandler,
                                        std::ifstream& schemaFileHandler) {
    // Check if data directory exists
    fs::path dataDirPath = fs::path(serverOptions.data_path);

    if (!fs::exists(dataDirPath)) {
        logger->log(Logger::level::INFO, Logger::group::SETUP,
                    "Data path didn't exist, creating directory.");
        try {
            fs::create_directory(dataDirPath);
        } catch (const std::exception& ex) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        "An error occurred while creating the data directory: " + std::string(ex.what()));
            return false;
        }
    }

    // Check if settings file exists
    fs::path settingsFilePath = dataDirPath/fs::path("settings.json");
    if (!fs::exists(settingsFilePath)) {
        logger->log(Logger::level::WARN, Logger::group::SETUP,
                    "Settings file didn't exist, creating one.");

        std::ofstream outputFileHandler;
        try {
            outputFileHandler = std::ofstream(settingsFilePath);
        } catch (const std::exception& ex) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        "An error occurred while creating the settings file: " + std::string(ex.what()));
            return false;
        }

        if (!generateDefaultSettingsJSON(serverOptions)) return false;

        outputFileHandler << std::setw(4) << settings;
    }

    try {
        settingsFileHandler = std::ifstream(settingsFilePath);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while opening the settings file: " + std::string(ex.what()));
        return false;
    }

    fs::path schemaFilePath = fs::path("settings.schema.json");
    if (!fs::exists(schemaFilePath) || !fs::is_regular_file(schemaFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Settings schema file didn't exist or wasn't a file, cannot validate settings file.");
        return false;
    }

    try {
        schemaFileHandler = std::ifstream(schemaFilePath);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while validating the settings file: " + std::string(ex.what()));
        return false;
    }

    return true;
}

bool SettingsManager::validateSettings(const argParser::options& serverOptions) {
    std::ifstream settingsFileHandler;
    std::ifstream schemaFileHandler;
    if (!openOrCreateFiles(serverOptions, settingsFileHandler, schemaFileHandler)) return false;

    try {
        settings = json::parse(settingsFileHandler);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while parsing the settings file: " + std::string(ex.what()));
        return false;
    }

    settingsFileHandler.close();

    json_validator schemaValidator;
    try {
        json schema = json::parse(schemaFileHandler);
        schemaValidator.set_root_schema(schema);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while parsing the settings schema file: " + std::string(ex.what()));
        return false;
    }

    // Check json structure
    try {
        schemaValidator.validate(settings);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The settings file does not have the correct structure: " + std::string(ex.what()));
        return false;
    }

    return true;
}

bool SettingsManager::generateDefaultSettingsJSON(const argParser::options& serverOptions) {
    fs::path dataDirAbsPath = fs::absolute(fs::path(serverOptions.data_path));
    fs::path certsPath = dataDirAbsPath/fs::path("certs");

    std::string tokenKeyString;
    std::string refreshTokenKeyString;
    std::string nexTokenKeyString;

    try {
        std::vector<unsigned char> tokenKey = crypto::genKey();
        tokenKeyString = crypto::base64Encode(tokenKey);

        std::vector<unsigned char> refreshTokenKey = crypto::genKey();
        refreshTokenKeyString = crypto::base64Encode(refreshTokenKey);

        std::vector<unsigned char> nexTokenKey = crypto::genKey();
        nexTokenKeyString = crypto::base64Encode(refreshTokenKey);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while generating the token keys: " + std::string(ex.what()));
        return false;
    }

    settings = {
            {"ssl", {
                    {"caCert", (certsPath/fs::path("ca.crt")).string()},
                    {"caKey", (certsPath/fs::path("ca.key")).string()},
                    {"cert", (certsPath/fs::path("any.nintendo.net.crt")).string()},
                    {"key", (certsPath/fs::path("any.nintendo.net.key")).string()}
            }},
            {"domain", "nintendo.net"},
            {"accounts", {
                    {"enabled", true},
                    {"tokenKey", tokenKeyString},
                    {"refreshTokenKey", refreshTokenKeyString},
                    {"deviceKeyPath", (certsPath/fs::path("device.key")).string()},
                    {"miiImagesPath", (dataDirAbsPath/fs::path("miis")).string()},
                    {"hosts", { // TODO Change this to a real address
                        {"00003200", {
                            {
                                {"address", "127.0.0.1:1201"},
                                {"grpcAddress", "127.0.0.1:1999"}
                            }
                        }},
                        {"10162B00", {
                            {
                                {"address", "127.0.0.1:1203"},
                                {"grpcAddress", "127.0.0.1:1999"}
                            }
                        }}
                    }},
                    {"secureServerGrpcAddresses", {
                        {"00003200", {"127.0.0.1:1999"}},
                        {"10162B00", {"127.0.0.1:1999"}}
                    }},
                    {"db", {
                        {"type", "SQLite3"},
                        {"path", (dataDirAbsPath/fs::path("account.db")).string()}
                    }},
                    {"email", {
                        {"enabled", false}
                    }},
                    {"allowRealWiiU", true},
                    {"allowGeneratedWiiU", true},
                    {"grpcRequestTimeout", 3000}, // in milliseconds
                    {"grpcConnectionPoolMaxSize", 1}
            }},
            {"boss", {
                    {"enabled", true},
                    {"db", {
                        {"type", "SQLite3"},
                        {"path", (dataDirAbsPath/fs::path("boss.db")).string()}
                    }}
            }},
            {"http", {
                    {"listenAddress", "0.0.0.0"},
                    {"listenPort", 443},
                    {"workerCount", 3}, // TODO Make this dynamic depending on the machine CPU thread count
                    {"keepAliveTimeout", 10},
                    {"ssl", true}
            }},
            {"grpc", {
                    {"enabled", true},
                    {"listenAddress", "0.0.0.0"},
                    {"port", 1999},
                    {"reflection", false},
                    {"publicFacingAddress", "127.0.0.1:1999"}
            }},
            {"sharedState", {
                    {"type", "local"}
            }},
            {"friendsAuth", {
                    {"enabled", true},
                    {"listenAddress", "0.0.0.0"},
                    {"port", 1201},
                    {"workerCount", 3},
                    {"secure", {
                        {"address", "127.0.0.1"},
                        {"port", 1202}
                    }},
                    {"db", {
                        {"type", "SQLite3"},
                        {"path", (dataDirAbsPath/fs::path("friendsAuth.db")).string()}
                    }}
            }},
            {"splatoonAuth", {
                    {"enabled", true},
                    {"listenAddress", "0.0.0.0"},
                    {"port", 1203},
                    {"workerCount", 3},
                    {"secure", {
                       {"address", "127.0.0.1"},
                       {"port", 1204}
                   }},
                   {"db", {
                       {"type", "SQLite3"},
                       {"path", (dataDirAbsPath/fs::path("splatoonAuth.db")).string()}
                   }}
            }},
            {"friendsSecure", {
                    {"enabled", true},
                    {"listenAddress", "0.0.0.0"},
                    {"port", 1202},
                    {"workerCount", 3},
                    {"db", {
                        {"type", "SQLite3"},
                        {"path", (dataDirAbsPath/fs::path("friendsSecure.db")).string()}
                    }},
                    {"grpcRequestTimeout", 3000}, // in milliseconds
                    {"grpcConnectionPoolMaxSize", 1}
            }},
            {"splatoonSecure", {
                   {"enabled", true},
                   {"listenAddress", "0.0.0.0"},
                   {"port", 1204},
                   {"workerCount", 3},
                   {"grpcRequestTimeout", 3000}, // in milliseconds
                   {"grpcConnectionPoolMaxSize", 1}
           }},
           {"management", {
                   {"enabled", true},
                   {"listenAddress", "0.0.0.0"},
                   {"listenPort", 3000},
                   {"workerCount", 1},
                   {"keepAliveTimeout", 10},
                   {"servers", {
                       {"accounts", {"127.0.0.1:1999"}},
                       {"boss", {"127.0.0.1:1999"}},
                       {"friendsAuth", {"127.0.0.1:1999"}},
                       {"friendsSecure", {"127.0.0.1:1999"}},
                       {"splatoonAuth", {"127.0.0.1:1999"}},
                       {"splatoonSecure", {"127.0.0.1:1999"}}
                   }},
                   {"grpcRequestTimeout", 3000}, // in milliseconds
                   {"grpcConnectionPoolMaxSize", 1},
                   {"corsOrigin", "*"}
           }},
           {"nex", {
                   {"tokenKey", nexTokenKeyString},
                   {"serverId", 0}
           }}
    };

    try {
        if (!fs::exists(certsPath)) fs::create_directory(certsPath);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while creating the certs and boss directories: " + std::string(ex.what()));
        return false;
    }

    return true;
}

// GETTERS

bool SettingsManager::isAccountEnabled() const {
    return enabledServers.account;
}

bool SettingsManager::allowRealWiiU() const {
    return settings["accounts"]["allowRealWiiU"];
}

bool SettingsManager::allowGeneratedWiiU() const {
    return settings["accounts"]["allowGeneratedWiiU"];
}

bool SettingsManager::isBOSSEnabled() const {
    return enabledServers.boss;
}

bool SettingsManager::isFriendsAuthEnabled() const {
    return enabledServers.friendsAuth;
}

bool SettingsManager::isFriendsSecureEnabled() const {
    return enabledServers.friendsSecure;
}

bool SettingsManager::isSplatoonAuthEnabled() const {
    return enabledServers.splatoonAuth;
}

bool SettingsManager::isSplatoonSecureEnabled() const {
    return enabledServers.splatoonSecure;
}

bool SettingsManager::isgRPCEnabled() const {
    return enabledServers.gRPC;
}

bool SettingsManager::isManagementEnabled() const {
    return settings["management"]["enabled"];
}

bool SettingsManager::hasCAKey() const {
    return settings["ssl"].contains("caKey");
}

fs::path SettingsManager::getSSLCertPath() const {
    return settings["ssl"]["cert"];
}

fs::path SettingsManager::getSSLCACertPath() const {
    return settings["ssl"]["caCert"];
}

fs::path SettingsManager::getSSLKeyPath() const {
    return settings["ssl"]["key"];
}

fs::path SettingsManager::getSSLCAKeyPath() const {
    return settings["ssl"]["caKey"];
}

fs::path SettingsManager::getDeviceKeyPath() const {
    return settings["accounts"]["deviceKeyPath"];
}

std::string SettingsManager::getTopDomain() const {
    return settings["domain"];
}

fs::path SettingsManager::getMiiImagesPath() const {
    return settings["accounts"]["miiImagesPath"];
}

sock::IPv4Addr SettingsManager::getHTTPListenAddress() const {
    std::string addressStr = settings["http"]["listenAddress"].get<std::string>();
    bool sslEnabled = settings["http"]["ssl"];

    sock::IPv4Addr address = util::stringToIPv4(addressStr);
    if (settings["http"].contains("listenPort")) address.port = settings["http"]["listenPort"].get<uint16_t>();
    else address.port = sslEnabled ? 443 : 80;

    return address;
}

bool SettingsManager::isHTTP_SSL_Enabled() const {
    return settings["http"]["ssl"];
}

sock::IPv4Addr SettingsManager::getManagementListenAddress() const {
    std::string addressStr = settings["management"]["listenAddress"].get<std::string>();

    sock::IPv4Addr address = util::stringToIPv4(addressStr);
    address.port = settings["management"]["listenPort"].get<uint16_t>();

    return address;
}

int SettingsManager::getManagementWorkerCount() const {
    return settings["management"]["workerCount"];
}

int SettingsManager::getManagementKeepAliveTimeout() const {
    return settings["management"]["keepAliveTimeout"];
}

std::set<sock::IPv4Addr> SettingsManager::getKnownProxies() const {
    std::set<sock::IPv4Addr> knownProxies;

    // Check if the settings file contains the knownProxies key
    if (!settings["nex"].contains("knownProxies")) return knownProxies;

    for (const auto& proxy : settings["nex"]["knownProxies"]) {
        knownProxies.insert(util::stringToIPv4(proxy.get<std::string>()));
    }

    return knownProxies;
}

int SettingsManager::getHTTPWorkerCount() const {
    return settings["http"]["workerCount"];
}

int SettingsManager::getHTTPKeepAliveTimeout() const {
    return settings["http"]["keepAliveTimeout"];
}

sock::IPv4Addr SettingsManager::getFriendsAuthListenAddress() const {
    sock::IPv4Addr address = util::stringToIPv4(settings["friendsAuth"]["listenAddress"].get<std::string>());
    address.port = settings["friendsAuth"]["port"];

    return address;
}

int SettingsManager::getFriendsAuthWorkerCount() const {
    return settings["friendsAuth"]["workerCount"];
}

sock::IPv4Addr SettingsManager::getFriendsSecureServerAddress() const {
    sock::IPv4Addr address = util::stringToIPv4(settings["friendsAuth"]["secure"]["address"].get<std::string>());
    address.port = settings["friendsAuth"]["secure"]["port"];

    return address;
}

int SettingsManager::getFriendsSecuregRPCRequestTimeout() const {
    return settings["friendsSecure"]["grpcRequestTimeout"];
}

int SettingsManager::getFriendsSecuregRPCConnectionPoolMaxSize() const {
    return settings["friendsSecure"]["grpcConnectionPoolMaxSize"];
}

sock::IPv4Addr SettingsManager::getFriendsSecureListenAddress() const {
    sock::IPv4Addr address = util::stringToIPv4(settings["friendsSecure"]["listenAddress"].get<std::string>());
    address.port = settings["friendsSecure"]["port"];

    return address;
}

int SettingsManager::getFriendsSecureWorkerCount() const {
    return settings["friendsSecure"]["workerCount"];
}

sock::IPv4Addr SettingsManager::getSplatoonAuthListenAddress() const {
    sock::IPv4Addr address = util::stringToIPv4(settings["splatoonAuth"]["listenAddress"].get<std::string>());
    address.port = settings["splatoonAuth"]["port"];

    return address;
}

int SettingsManager::getSplatoonAuthWorkerCount() const {
    return settings["splatoonAuth"]["workerCount"];
}

sock::IPv4Addr SettingsManager::getSplatoonSecureServerAddress() const {
    sock::IPv4Addr address = util::stringToIPv4(settings["splatoonAuth"]["secure"]["address"].get<std::string>());
    address.port = settings["splatoonAuth"]["secure"]["port"];

    return address;
}

sock::IPv4Addr SettingsManager::getSplatoonSecureListenAddress() const {
    sock::IPv4Addr address = util::stringToIPv4(settings["splatoonSecure"]["listenAddress"].get<std::string>());
    address.port = settings["splatoonSecure"]["port"];

    return address;
}

int SettingsManager::getSplatoonSecureWorkerCount() const {
    return settings["splatoonSecure"]["workerCount"];
}

int SettingsManager::getSplatoonSecuregRPCRequestTimeout() const {
    return settings["splatoonSecure"]["grpcRequestTimeout"];
}

int SettingsManager::getSplatoonSecuregRPCConnectionPoolMaxSize() const {
    return settings["splatoonSecure"]["grpcConnectionPoolMaxSize"];
}

sock::IPv4Addr SettingsManager::getgRPCListenAddress() const {
    sock::IPv4Addr address = util::stringToIPv4(settings["grpc"]["listenAddress"].get<std::string>());
    address.port = settings["grpc"]["port"];

    return address;
}

std::string SettingsManager::getgRCPPublicFacingAddress() const {
    return settings["grpc"]["publicFacingAddress"];
}

bool SettingsManager::isgRPCReflectionEnabled() const {
    return settings["grpc"].contains("reflection") && settings["grpc"]["reflection"].get<bool>();
}

json SettingsManager::getAccountsDBSettings() const {
    return settings["accounts"]["db"];
}

std::string SettingsManager::getSharedStateType() const {
    if (settings.contains("sharedState") && settings["sharedState"].contains("type")) {
        return settings["sharedState"]["type"].get<std::string>();
    }
    return "local"; // Default to local if not specified
}

json SettingsManager::getSharedStateRedisSettings() const {
    if (settings.contains("sharedState") && settings["sharedState"].contains("redis")) {
        return settings["sharedState"]["redis"];
    }
    return json::object(); // Return empty object if not configured
}

json SettingsManager::getFriendsAuthDBSettings() const {
    return settings["friendsAuth"]["db"];
}

json SettingsManager::getFriendsSecureDBSettings() const {
    return settings["friendsSecure"]["db"];
}

json SettingsManager::getSplatoonAuthDBSettings() const {
    return settings["splatoonAuth"]["db"];
}

json SettingsManager::getBOSSDBSettings() const {
    return settings["boss"]["db"];
}

std::vector<std::string> SettingsManager::getDomains() const {
    std::vector<std::string> usedDomains;

    if (!domains.account.empty()) usedDomains.push_back(domains.account);
    if (!domains.miiSecure.empty()) usedDomains.push_back(domains.miiSecure);
    if (!domains.bossNPTS.empty()) usedDomains.push_back(domains.bossNPTS);
    if (!domains.bossNPPL.empty()) usedDomains.push_back(domains.bossNPPL);
    if (!domains.bossNPDI.empty()) usedDomains.push_back(domains.bossNPDI);

    return usedDomains;
}

std::string SettingsManager::getTokenKey() const {
    return settings["accounts"]["tokenKey"];
}

std::string SettingsManager::getRefreshTokenKey() const {
    return settings["accounts"]["refreshTokenKey"];
}

std::string SettingsManager::getNEXTokenKey() const {
    return settings["nex"]["tokenKey"];
}

uint32_t SettingsManager::getNEXServerID() const {
    return settings["nex"]["serverId"];
}

std::map<std::string, std::vector<std::pair<std::string, std::string>>> SettingsManager::getGameServerHosts() const {
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> hosts;

    for (const auto& [id, hostList] : settings["accounts"]["hosts"].items()) {
        std::vector<std::pair<std::string, std::string>> hostPairs;
        for (const auto& hostInfo : hostList) {
            hostPairs.emplace_back(hostInfo["address"], hostInfo["grpcAddress"]);
        }
        hosts[id] = std::move(hostPairs);
    }

    return std::move(hosts);
}

std::map<std::string, std::vector<std::string>> SettingsManager::getGameServergRPCHosts() const {
    std::map<std::string, std::vector<std::string>> hosts;

    for (const auto& [id, grpcAddressList] : settings["accounts"]["secureServerGrpcAddresses"].items()) {
        std::vector<std::string> grpcAddresses;
        for (const auto& grpcAddress : grpcAddressList) {
            grpcAddresses.push_back(grpcAddress.get<std::string>());
        }
        hosts[id] = std::move(grpcAddresses);
    }

    return std::move(hosts);
}

int SettingsManager::getAccountsgRPCRequestTimeout() const {
    return settings["accounts"]["grpcRequestTimeout"];
}

int SettingsManager::getAccountsgRPCConnectionPoolMaxSize() const {
    return settings["accounts"]["grpcConnectionPoolMaxSize"];
}

std::map<ServerType, std::vector<sock::IPv4Addr>> SettingsManager::getManagementServerAddresses() const {
    std::map<ServerType, std::vector<sock::IPv4Addr>> serverAddresses;

    for (const auto& [serverName, addressList] : settings["management"]["servers"].items()) {
        ServerType serverType;
        if (serverName == "accounts") serverType = ServerType::ACCOUNT;
        else if (serverName == "boss") serverType = ServerType::BOSS;
        else if (serverName == "friendsAuth") serverType = ServerType::FRIENDS_AUTH;
        else if (serverName == "friendsSecure") serverType = ServerType::FRIENDS_SECURE;
        else if (serverName == "splatoonAuth") serverType = ServerType::SPLATOON_AUTH;
        else if (serverName == "splatoonSecure") serverType = ServerType::SPLATOON_SECURE;
        else continue;

        std::vector<sock::IPv4Addr> addresses;
        for (const auto& addressStr : addressList) {
            addresses.push_back(util::stringToIPv4WPort(addressStr.get<std::string>()));
        }

        serverAddresses[serverType] = std::move(addresses);
    }

    return serverAddresses;
}

std::string SettingsManager::getManagementCORSAllowedOrigin() const {
    return settings["management"]["corsOrigin"];
}

int SettingsManager::getManagementgRPCRequestTimeout() const {
    return settings["management"]["grpcRequestTimeout"];
}

int SettingsManager::getManagementgRPCConnectionPoolMaxSize() const {
    return settings["management"]["grpcConnectionPoolMaxSize"];
}

bool SettingsManager::isAccountsEmailEnabled() const {
    return settings["accounts"]["email"]["enabled"].get<bool>();
}

json SettingsManager::getAccountsEmailSettings() const {
    return settings["accounts"]["email"];
}