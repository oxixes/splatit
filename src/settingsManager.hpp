#ifndef SPLATOON_SERVER_SETTINGSMANAGER_HPP
#define SPLATOON_SERVER_SETTINGSMANAGER_HPP

#include <string>

#include <nlohmann/json.hpp>

class SettingsManager {
public:
    SettingsManager(const std::string& dataPath);
    ~SettingsManager();

    void init();

private:
    std::string dataPath;

    void validateSettings();
};

#endif //SPLATOON_SERVER_SETTINGSMANAGER_HPP
