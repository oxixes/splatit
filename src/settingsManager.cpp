#include "settingsManager.hpp"

SettingsManager::SettingsManager(Logger::Logger* logger) {
    this->logger = logger;
}

bool SettingsManager::init(const argParser::options& serverOptions) {
    return validateSettings(serverOptions);
}

bool SettingsManager::openOrCreateFiles(const argParser::options &serverOptions, std::ifstream& settingsFileHandler) {
    // Check if data directory exists
    fs::path dataDirPath = fs::path(serverOptions.data_path);

    if (!fs::exists(dataDirPath)) {
        logger->log(Logger::level::INFO, Logger::group::SETUP,
                    "Data path didn't exist, creating directory.");
        try {
            fs::create_directory(dataDirPath);
        } catch (const std::exception& ex) {
            logger->log(Logger::level::ERROR, Logger::group::SETUP,
                        "An error occurred while creating the data directory: " + std::string(ex.what()));
            return false;
        }
    }

    // Check if settings file exists
    std::filesystem::path settingsFilePath = dataDirPath/fs::path("settings.json");
    if (!fs::exists(settingsFilePath)) {
        logger->log(Logger::level::WARN, Logger::group::SETUP,
                    "Settings file didn't exist, creating one.");

        std::ofstream outputFileHandler;
        try {
            outputFileHandler = std::ofstream(settingsFilePath);
        } catch (const std::exception& ex) {
            logger->log(Logger::level::ERROR, Logger::group::SETUP,
                        "An error occurred while creating the settings file: " + std::string(ex.what()));
            return false;
        }

        if (!generateDefaultSettingsJSON(serverOptions)) return false;

        outputFileHandler << std::setw(4) << settings;
    }

    try {
        settingsFileHandler = std::ifstream(settingsFilePath);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    "An error occurred while opening the settings file: " + std::string(ex.what()));
        return false;
    }

    return true;
}

bool SettingsManager::validateSettings(const argParser::options& serverOptions) {
    std::ifstream settingsFileHandler;
    if (!openOrCreateFiles(serverOptions, settingsFileHandler)) return false;

    try {
        settings = json::parse(settingsFileHandler);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    "An error occurred while parsing the settings file: " + std::string(ex.what()));
        return false;
    }

    settingsFileHandler.close();

    // Check json structure
    if (!settings.contains("ssl")
        || !settings.contains("domain")
        || !settings.contains("boss")
        || !settings["ssl"].contains("caCert") || !settings["ssl"].contains("caKey")
        || !settings["ssl"].contains("cert") || !settings["ssl"].contains("key")
        || !settings["boss"].contains("data")) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    "The settings file does not have the correct structure, please check"
                    " the docs to create a correct file.");
        return false;
    }

    return true;
}

bool SettingsManager::generateDefaultSettingsJSON(const argParser::options& serverOptions) {
    fs::path dataDirAbsPath = fs::absolute(fs::path(serverOptions.data_path));
    fs::path certsPath = dataDirAbsPath/fs::path("certs");
    fs::path bossPath = dataDirAbsPath/fs::path("boss");

    settings = {
            {"ssl", {
                    {"caCert", (certsPath/fs::path("ca.crt")).string()},
                    {"caKey", (certsPath/fs::path("ca.key")).string()},
                    {"cert", (certsPath/fs::path("any.nintendo.net.crt")).string()},
                    {"key", (certsPath/fs::path("any.nintendo.net.key")).string()}
            }},
            {"domain", "nintendo.net"},
            {"boss", {
                    {"data", bossPath.string()}
            }}
    };

    try {
        if (!fs::exists(certsPath)) fs::create_directory(certsPath);
        if (!fs::exists(bossPath)) fs::create_directory(bossPath);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    "An error occurred while creating the certs and boss directories: " + std::string(ex.what()));
        return false;
    }

    return true;
}

// GETTERS

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

fs::path SettingsManager::getDomain() const {
    return settings["domain"];
}

fs::path SettingsManager::getBOSSPath() const {
    return settings["boss"]["data"];
}