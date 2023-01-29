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

    std::string getSSLCertPath() const;
    std::string getSSLCACertPath() const;
    std::string getSSLKeyPath() const;
    std::string getSSLCAKeyPath() const;
    std::string getDomain() const;
    std::string getBOSSPath() const;
private:
    json settings;
    Logger::Logger* logger;

    bool openOrCreateFiles(const argParser::options& serverOptions, std::ifstream& settingsFileHandler);
    bool validateSettings(const argParser::options& serverOptions);
    bool generateDefaultSettingsJSON(const argParser::options& serverOptions);
};

#endif //SPLATOON_SERVER_SETTINGSMANAGER_HPP
