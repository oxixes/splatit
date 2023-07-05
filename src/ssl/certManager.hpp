#ifndef SPLATOON_SERVER_CERTMANAGER_HPP
#define SPLATOON_SERVER_CERTMANAGER_HPP

#include <string>
#include <vector>
#include <filesystem>

#include "../settingsManager.hpp"
#include "../logger.hpp"
#include "../util/util.hpp"

// Windows libraries present in socket.hpp imported from settingsManager.hpp
// conflict with some OpenSSL names, so we include them here instead.
// Why Microsoft, why do you make this so hard?

#include <nlohmann/json.hpp>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/engine.h>

namespace fs = std::filesystem;

class CertManager {
public:
    CertManager(std::shared_ptr<SettingsManager> settingsManager, std::shared_ptr<Logger::Logger> logger);
    ~CertManager();

    bool init();
    EVP_PKEY* getSSLKey();
    X509* getSSLCert();
    void cleanup();

private:
    std::shared_ptr<SettingsManager> settingsManager;
    std::shared_ptr<Logger::Logger> logger;

    EVP_PKEY* CAkey;
    X509* CAcert;
    EVP_PKEY* key;
    X509* cert;

    bool loadKey(const fs::path& keyPath, EVP_PKEY** pKey);
    bool loadCert(const fs::path& certPath, X509** outCert);
    bool writeKey(EVP_PKEY* pKey, const fs::path& keyPath);
    bool writeCert(X509* crt, const fs::path& certPath);

    bool validateCert(X509* crt, EVP_PKEY* pKey);
    bool validateRSAKey(EVP_PKEY* pKey);
    bool validateCA(X509* crt, EVP_PKEY* pKey);
    bool validateSSLCert(X509* crt, EVP_PKEY* pKey, X509* CAcrt, const std::vector<std::string>& domains);

    bool genRSAKey(EVP_PKEY** pKey);

    bool createCA(const fs::path& crtFile, const fs::path& keyFile, EVP_PKEY* pKey, X509** outCert);
    bool createSSLServerCert(X509* caCert, EVP_PKEY* caKey, const fs::path& crtFile,
                             const fs::path& keyFile, const std::vector<std::string>& domains,
                             EVP_PKEY* pKey, X509** outCert);

    static void addExtToCert(X509* ca, X509* cert, int nid, const std::string& value);
};

#endif //SPLATOON_SERVER_CERTMANAGER_HPP
