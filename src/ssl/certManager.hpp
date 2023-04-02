#ifndef SPLATOON_SERVER_CERTMANAGER_HPP
#define SPLATOON_SERVER_CERTMANAGER_HPP

#include <string>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/engine.h>

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
    bool loadKey(const fs::path& keyPath, EVP_PKEY** pKey);
    bool loadCert(const fs::path& certPath, X509** cert);
    bool writeKey(EVP_PKEY* pKey, const fs::path& keyPath);
    bool writeCert(X509* cert, const fs::path& certPath);

    bool validateCert(X509* cert, EVP_PKEY* pKey);
    bool validateRSAKey(EVP_PKEY* pKey);
    bool validateCA(X509* cert, EVP_PKEY* pKey);
    bool validateSSLCert(X509* cert, EVP_PKEY* pKey, X509* CAcert, const std::vector<std::string>& domains);

    bool genRSAKey(EVP_PKEY** pKey);

    bool createCA(const fs::path& path, const std::string& filename, EVP_PKEY** pKey, X509** cert);
    bool createSSLServerCert(X509* caCert, EVP_PKEY* caKey, const fs::path& path,
                             const std::string& filename, const std::vector<std::string>& domains,
                             EVP_PKEY** pKey, X509** cert);

    static void addExtToCert(X509* ca, X509* cert, int nid, const std::string& value);
    static std::string getOpenSSLerror();
};

#endif //SPLATOON_SERVER_CERTMANAGER_HPP
