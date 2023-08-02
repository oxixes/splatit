#include "settingsManager.hpp"
#include "crypto/tools.hpp"

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
    }

    if (settings.contains("boss") && settings["boss"]["enabled"]) {
        enabledServers.boss = true;
        domains.bossNPTS = "npts.app." + std::string(settings["domain"]);
        domains.bossNPPL = "nppl.app." + std::string(settings["domain"]);
        domains.bossNPDI = "npdi.cdn." + std::string(settings["domain"]);
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
        std::vector<unsigned char> tokenKey = crypto::genSHA256Key();
        tokenKeyString = crypto::base64Encode(tokenKey);

        std::vector<unsigned char> refreshTokenKey = crypto::genSHA256Key();
        refreshTokenKeyString = crypto::base64Encode(refreshTokenKey);

        std::vector<unsigned char> nexTokenKey = crypto::genSHA256Key();
        nexTokenKeyString = crypto::base64Encode(refreshTokenKey);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "An error occurred while generating the token keys: " + std::string(ex.what()));
        return false;
    }

    settings = {
            {"db", {
                    {"type", "SQLite3"},
                    {"path", (dataDirAbsPath/fs::path("db.db")).string()}
            }},
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
                    {"hosts", { // TODO Change this to a real address
                        {"00003200", "127.0.0.1:1201"},
                        {"10162B00", "127.0.0.1:1203"}
                    }},
                    {"allowRealWiiU", true},
                    {"allowGeneratedWiiU", true}
            }},
            {"boss", {
                    {"enabled", true},
                    {"path", (dataDirAbsPath/fs::path("boss")).string()}
            }},
            {"http", {
                    {"listenAddress", "0.0.0.0"},
                    {"workerCount", 3}, // TODO Make this dynamic depending on the machine CPU thread count
                    {"keepAliveTimeout", 10}
            }},
            {"nex", {
                    {"tokenKey", nexTokenKeyString}
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

fs::path SettingsManager::getBOSSPath() const {
    return settings["boss"]["path"];
}

sock::IPv4Addr SettingsManager::getHTTPListenAddress() const {
    std::string addressStr = settings["http"]["listenAddress"].get<std::string>();

    auto a = (uint8_t) std::stoi(addressStr.substr(0, addressStr.find('.')));
    addressStr = addressStr.substr(addressStr.find('.') + 1);
    auto b = (uint8_t) std::stoi(addressStr.substr(0, addressStr.find('.')));
    addressStr = addressStr.substr(addressStr.find('.') + 1);
    auto c = (uint8_t) std::stoi(addressStr.substr(0, addressStr.find('.')));
    addressStr = addressStr.substr(addressStr.find('.') + 1);
    auto d = (uint8_t) std::stoi(addressStr);


    sock::IPv4Addr address{a, b, c, d, 443};
    return address;
}

int SettingsManager::getHTTPWorkerCount() const {
    return settings["http"]["workerCount"];
}

int SettingsManager::getHTTPKeepAliveTimeout() const {
    return settings["http"]["keepAliveTimeout"];
}

json SettingsManager::getDBSettings() const {
    return settings["db"];
}

std::vector<std::string> SettingsManager::getDomains() const {
    std::vector<std::string> usedDomains;

    if (!domains.account.empty()) usedDomains.push_back(domains.account);
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

std::string SettingsManager::getGameServerHost(const std::string& id) const {
    return settings["accounts"]["hosts"][id];
}