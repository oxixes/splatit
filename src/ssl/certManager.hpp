#ifndef SPLATOON_SERVER_CERTMANAGER_HPP
#define SPLATOON_SERVER_CERTMANAGER_HPP

#include <string>
#include <vector>
#include <filesystem>

#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#include "../settingsManager.hpp"
#include "../logger.hpp"

namespace fs = std::filesystem;

class CertManager {
public:
    explicit CertManager(SettingsManager* settingsManager, Logger::Logger* logger);

private:
    SettingsManager* settingsManager;
    Logger::Logger* logger;

public:
    bool createCA(const fs::path& path, const std::string& filename, EVP_PKEY* privKey, X509* CAcert);
    bool createSSLCert(const std::string& caCertPath, const std::string& caKeyPath,
                       const std::string& path, const std::string& filename, const std::vector<std::string>& domains);

    static void addExtToCert(X509* cert, int nid, const std::string& value);
    static std::string getOpenSSLerror();
};

#endif //SPLATOON_SERVER_CERTMANAGER_HPP
