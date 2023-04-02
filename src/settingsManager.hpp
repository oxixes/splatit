#ifndef SPLATOON_SERVER_SETTINGSMANAGER_HPP
#define SPLATOON_SERVER_SETTINGSMANAGER_HPP

#include <string>
#include <fstream>
#include <sys/stat.h>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "argParser.hpp"
#include "logger.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

class SettingsManager {
public:
    explicit SettingsManager(Logger::Logger* logger);
    ~SettingsManager() = default;

    bool init(const argParser::options& serverOptions);

    fs::path getSSLCertPath() const;
    fs::path getSSLCACertPath() const;
    fs::path getSSLKeyPath() const;
    fs::path getSSLCAKeyPath() const;
    fs::path getDomain() const;
    fs::path getBOSSPath() const;
private:
    json settings;
    Logger::Logger* logger;

    bool openOrCreateFiles(const argParser::options& serverOptions, std::ifstream& settingsFileHandler);
    bool validateSettings(const argParser::options& serverOptions);
    bool generateDefaultSettingsJSON(const argParser::options& serverOptions);
};

#endif //SPLATOON_SERVER_SETTINGSMANAGER_HPP
