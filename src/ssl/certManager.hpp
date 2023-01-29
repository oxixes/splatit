#ifndef SPLATOON_SERVER_CERTMANAGER_HPP
#define SPLATOON_SERVER_CERTMANAGER_HPP

#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>

#include "../settingsManager.hpp"

class CertManager {
public:
    explicit CertManager(SettingsManager* settingsManager);

private:
    SettingsManager* settingsManager;
};

#endif //SPLATOON_SERVER_CERTMANAGER_HPP
