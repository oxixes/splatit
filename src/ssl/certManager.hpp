#ifndef SPLATOON_SERVER_CERTMANAGER_HPP
#define SPLATOON_SERVER_CERTMANAGER_HPP

#include <string>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#include "../settingsManager.hpp"
#include "../logger.hpp"

namespace fs = std::filesystem;

class CertManager {
public:
    CertManager(SettingsManager* settingsManager, Logger::Logger* logger);

private:
    SettingsManager* settingsManager;
    Logger::Logger* logger;

public:
    bool createCA(const fs::path& path, const std::string& filename, EVP_PKEY** pKey, X509** cert);
    bool createSSLCert(X509** caCert, EVP_PKEY** caKey, const fs::path& path,
                       const std::string& filename, const std::vector<std::string>& domains,
                       EVP_PKEY** pKey, X509** cert);

    bool writeKeyAndCertToDisk(EVP_PKEY** pKey, X509** cert, const fs::path& keyPath, const fs::path& certPath);
    static void addExtToCert(X509* cert, int nid, const std::string& value);
    static std::string getOpenSSLerror();
};

#endif //SPLATOON_SERVER_CERTMANAGER_HPP
