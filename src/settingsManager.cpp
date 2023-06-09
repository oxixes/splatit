#include "settingsManager.hpp"

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

    // TODO Add BOSS and more to settings file

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
    if (!fs::exists(schemaFilePath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "Settings schema file didn't exist, cannot validate settings file.");
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
    //fs::path bossPath = dataDirAbsPath/fs::path("boss");

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
                    {"enabled", true}
            }}
    };

    try {
        if (!fs::exists(certsPath)) fs::create_directory(certsPath);
        //if (!fs::exists(bossPath)) fs::create_directory(bossPath);
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

fs::path SettingsManager::getTopDomain() const {
    return settings["domain"];
}

fs::path SettingsManager::getBOSSPath() const {
    return settings["boss"]["data"];
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