#include "certManager.hpp"

#include <utility>
#include <regex>

#include "tools.hpp"
#include "../exceptions.hpp"

namespace crypto {

CertManager::CertManager(std::shared_ptr<SettingsManager> settingsManager, std::shared_ptr<Logger::Logger> logger) {
    this->settingsManager = std::move(settingsManager);
    this->logger = std::move(logger);

    this->CAkey = nullptr;
    this->CAcert = nullptr;
    this->key = nullptr;
    this->cert = nullptr;
    this->deviceKey = nullptr;
}

CertManager::~CertManager() {
    cleanup();
}

bool CertManager::init() {
    if ((settingsManager->isAccountEnabled() || settingsManager->isBOSSEnabled()) && settingsManager->isHTTP_SSL_Enabled()) {
        if (settingsManager->hasCAKey()) {
            if (!util::checkParentDirectory(settingsManager->getSSLCAKeyPath())) {
                logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                            settingsManager->getSSLCAKeyPath().parent_path().string() + " is not a directory!");
                return false;
            }

            if (fs::exists(settingsManager->getSSLCAKeyPath())) {
                logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading CA key...");
                if (!loadKey(settingsManager->getSSLCAKeyPath(), &CAkey)) return false;
                if (!validateRSAKey(CAkey)) return false;
            } else {
                logger->log(Logger::level::INFO, Logger::group::SETUP, "CA key not found!");
                if (!genRSAKey(&CAkey)) return false;
            }
        }

        if (!util::checkParentDirectory(settingsManager->getSSLCACertPath())) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        settingsManager->getSSLCACertPath().parent_path().string() + " is not a directory!");
            return false;
        }

        if (fs::exists(settingsManager->getSSLCACertPath())) {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading CA cert...");
            if (!loadCert(settingsManager->getSSLCACertPath(), &CAcert)) return false;
            if (!validateCA(CAcert, CAkey)) return false;
        } else {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "CA cert not found!");
            if (CAkey == nullptr) {
                logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                            "CA key is required to create the CA certificate!");
                return false;
            }
            if (!createCA(settingsManager->getSSLCACertPath(),settingsManager->getSSLCAKeyPath(),
                          CAkey, &CAcert)) return false;
        }

        if (!util::checkParentDirectory(settingsManager->getSSLKeyPath())) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        settingsManager->getSSLKeyPath().parent_path().string() + " is not a directory!");
            return false;
        }

        if (fs::exists(settingsManager->getSSLKeyPath())) {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading key...");
            if (!loadKey(settingsManager->getSSLKeyPath(), &key)) return false;
            if (!validateRSAKey(key)) return false;
        } else {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Key not found!");
            if (!genRSAKey(&key)) return false;
        }

        if (!util::checkParentDirectory(settingsManager->getSSLCertPath())) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        settingsManager->getSSLCertPath().parent_path().string() + " is not a directory!");
            return false;
        }

        if (fs::exists(settingsManager->getSSLCertPath())) {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading SSL certificate...");
            if (!loadCert(settingsManager->getSSLCertPath(), &cert)) return false;
            if (!validateSSLCert(cert, key, CAcert, settingsManager->getDomains())) return false;
        } else {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Cert not found!");
            if (CAkey == nullptr) {
                logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                            "CA key is required to create the SSL certificate!");
                return false;
            }
            if (!createSSLServerCert(CAcert, CAkey, settingsManager->getSSLCertPath(),
                                     settingsManager->getSSLKeyPath(), settingsManager->getDomains(),
                                     key, &cert)) return false;
        }
    }

    if (settingsManager->isAccountEnabled()) {
        if (!util::checkParentDirectory(settingsManager->getDeviceKeyPath())) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        settingsManager->getDeviceKeyPath().parent_path().string() + " is not a directory!");
            return false;
        }

        if (fs::exists(settingsManager->getDeviceKeyPath())) {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading device key...");
            if (!loadKey(settingsManager->getDeviceKeyPath(), &deviceKey)) return false;
            if (!validateECDSAKey(deviceKey)) return false;
        } else {
            logger->log(Logger::level::INFO, Logger::group::SETUP, "Device key not found!");
            if (!genECDSAKey(&deviceKey)) return false;
            if (!writeKey(deviceKey, settingsManager->getDeviceKeyPath())) return false;
        }
    }

    return true;
}

bool CertManager::initManagementTLS() {
    const fs::path certPath = settingsManager->getManagementSSLCertPath();
    const fs::path keyPath = settingsManager->getManagementSSLKeyPath();

    if (!fs::exists(certPath) || !fs::is_regular_file(certPath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The management certificate " + certPath.string() + " does not exist or is not a file.");
        return false;
    }

    if (!fs::exists(keyPath) || !fs::is_regular_file(keyPath)) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The management private key " + keyPath.string() + " does not exist or is not a file.");
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading management certificate...");
    if (!loadCert(certPath, &managementCert)) return false;
    if (!loadKey(keyPath, &managementKey)) return false;

    // Catch a mismatched pair here rather than during the first TLS handshake,
    // where it would surface as an opaque client side error.
    if (X509_check_private_key(managementCert, managementKey) != 1) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The management certificate and private key do not match: " + util::getOpenSSLError());
        return false;
    }

    return true;
}

void CertManager::cleanup() {
    if (managementCert) {
        X509_free(managementCert);
        managementCert = nullptr;
    }

    if (managementKey) {
        EVP_PKEY_free(managementKey);
        managementKey = nullptr;
    }

    if (cert) {
        X509_free(cert);
        cert = nullptr;
    }

    if (key) {
        EVP_PKEY_free(key);
        key = nullptr;
    }

    if (CAcert) {
        X509_free(CAcert);
        CAcert = nullptr;
    }

    if (CAkey) {
        EVP_PKEY_free(CAkey);
        CAkey = nullptr;
    }

    if (deviceKey) {
        EVP_PKEY_free(deviceKey);
        deviceKey = nullptr;
    }
}

bool CertManager::loadKey(const fs::path& keyPath, EVP_PKEY** pKey) {
    FILE* pKeyFile = fopen(keyPath.string().c_str(), "rb");

    if (pKeyFile == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The private key couldn't be loaded: " +
                                                                  std::string(strerror(errno)));
        return false;
    }

    *pKey = PEM_read_PrivateKey(pKeyFile, nullptr, nullptr, nullptr);

    if (*pKey == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The private key couldn't be loaded: " +
                                                                  util::getOpenSSLError());
        fclose(pKeyFile);
        return false;
    }

    fclose(pKeyFile);
    return true;
}

bool CertManager::loadCert(const fs::path& certPath, X509** outCert) {
    FILE* certFile = fopen(certPath.string().c_str(), "rb");

    if (certFile == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be loaded: " +
                                                                  std::string(strerror(errno)));
        return false;
    }

    *outCert = PEM_read_X509(certFile, nullptr, nullptr, nullptr);

    if (*outCert == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be loaded: " +
                                                                  util::getOpenSSLError());
        fclose(certFile);
        return false;
    }

    fclose(certFile);
    return true;
}

bool CertManager::writeKey(EVP_PKEY* pKey, const fs::path& keyPath) {
    FILE* pKeyFile = fopen(keyPath.string().c_str(), "wb");

    if (pKeyFile == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The private key couldn't be written to disk: " +
                                                                  std::string(strerror(errno)));
        return false;
    }

    if (PEM_write_PrivateKey(pKeyFile, pKey, nullptr, nullptr, 0, nullptr, nullptr) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The private key couldn't be written to disk: " +
                                                                  util::getOpenSSLError());
        fclose(pKeyFile);
        return false;
    }

    fclose(pKeyFile);
    return true;
}

bool CertManager::writeCert(X509 *crt, const fs::path &certPath) {
    FILE* certFile = fopen(certPath.string().c_str(), "wb");

    if (certFile == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be written to disk: " +
                                                                  std::string(strerror(errno)));
        return false;
    }

    if (PEM_write_X509(certFile, crt) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be written to disk: " +
                                                                  util::getOpenSSLError());
        fclose(certFile);
        return false;
    }

    fclose(certFile);
    return true;
}

bool CertManager::validateCA(X509* crt, EVP_PKEY* pKey) {
    if (!validateCert(crt, pKey)) return false;

    if (X509_check_ca(crt) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate is not a CA!");
        return false;
    }

    return true;
}

bool CertManager::validateSSLCert(X509* crt, EVP_PKEY* pKey, X509* CAcrt, const std::vector<std::string>& domains) {
    if (!validateCert(crt, pKey)) return false;

    // Check if the certificate is signed by the CA
    X509_STORE* store = X509_STORE_new();
    if(store == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be validated: " +
                                                                  util::getOpenSSLError());
        return false;
    }

    if(X509_STORE_add_cert(store, CAcrt) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be validated: " +
                                                                  util::getOpenSSLError());
        return false;
    }

    X509_STORE_CTX* storeCtx = X509_STORE_CTX_new();
    if(storeCtx == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be validated: " +
                                                                  util::getOpenSSLError());
        return false;
    }

    if (X509_STORE_CTX_init(storeCtx, store, crt, nullptr) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate couldn't be validated: " +
                                                                  util::getOpenSSLError());
        return false;
    }

    if (X509_verify_cert(storeCtx) != 1) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate is not signed by the CA or hasn't been issued correctly!");
        return false;
    }

    X509_STORE_CTX_free(storeCtx);
    X509_STORE_free(store);

    // Check if the certificate is a CA certificate
    if (X509_check_ca(crt) != 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate is a CA certificate!");
        return false;
    }

    // Check if the certificate is valid for the given domains
    std::vector<std::string> certDomains{};

    int extLoc = X509_get_ext_by_NID(crt, NID_subject_alt_name, -1);
    X509_EXTENSION* ext = X509_get_ext(crt, extLoc);
    if (ext == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The certificate doesn't contain the subject alternative name extension!");
        return false;
    }

    STACK_OF(GENERAL_NAME)* domainsList = (STACK_OF(GENERAL_NAME)*) X509V3_EXT_d2i(ext);
    if (domainsList == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                    "The certificate doesn't contain any domains!");
        return false;
    }

    for (int i = 0; i < sk_GENERAL_NAME_num(domainsList); i++) {
        GENERAL_NAME* domain = sk_GENERAL_NAME_value(domainsList, i);
        if (domain->type == GEN_DNS) {
            char* domainName = (char*) ASN1_STRING_get0_data(domain->d.dNSName);
            certDomains.emplace_back(domainName);
        }
    }

    bool invalid = false;
    for (const auto& domain : domains) {
        if (std::find(certDomains.begin(), certDomains.end(), domain) == certDomains.end()) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP,
                        "The certificate doesn't contain the domain " + domain + ".");
            invalid = true;
        }
    }

    sk_GENERAL_NAME_pop_free(domainsList, GENERAL_NAME_free);

    return !invalid;
}

bool CertManager::validateCert(X509* crt, EVP_PKEY* pKey) {
    if (pKey != nullptr) {
        if (!validateRSAKey(pKey)) return false;

        if (X509_check_private_key(crt, pKey) == 0) {
            logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate and the key don't match!");
            return false;
        }
    }

    if (X509_cmp_current_time(X509_get_notBefore(crt)) >= 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate is not valid yet!");
        return false;
    }

    if (X509_cmp_current_time(X509_get_notAfter(crt)) <= 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The certificate is expired!");
        return false;
    }

    return true;
}

bool CertManager::validateRSAKey(EVP_PKEY* pKey) {
    int keyType = EVP_PKEY_type(EVP_PKEY_get_id(pKey));

    if (keyType != EVP_PKEY_RSA && keyType != EVP_PKEY_RSA2) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The key is not an RSA key!");
        return false;
    }

    return true;
}

bool CertManager::validateECDSAKey(EVP_PKEY* pKey) {
    int keyType = EVP_PKEY_type(EVP_PKEY_get_id(pKey));

    if (keyType != EVP_PKEY_EC) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The key is not an ECDSA key!");
        return false;
    }

    return true;
}

bool CertManager::genRSAKey(EVP_PKEY** pKey, Logger::group logGroup) {
    logger->log(Logger::level::INFO, logGroup, "Generating RSA private key...");
    *pKey = EVP_RSA_gen(2048);

    if (*pKey == nullptr) {
        logger->log(Logger::level::FAILURE, logGroup, "The RSA key couldn't be generated: " +
                                                           util::getOpenSSLError());
        return false;
    }

    return true;
}

bool CertManager::genECDSAKey(EVP_PKEY** pKey, Logger::group logGroup) {
    logger->log(Logger::level::INFO, logGroup, "Generating ECDSA private key...");
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (ctx == nullptr) goto error;
    if (EVP_PKEY_keygen_init(ctx) <= 0) goto error;
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_sect233r1) <= 0) goto error;
    if (EVP_PKEY_keygen(ctx, pKey) <= 0) goto error;

    EVP_PKEY_CTX_free(ctx);
    return true;

    error:
    logger->log(Logger::level::FAILURE, logGroup, "The ECDSA key couldn't be generated: " +
                                                        util::getOpenSSLError());
    if (ctx != nullptr) EVP_PKEY_CTX_free(ctx);
    return false;
}

bool CertManager::createCA(const fs::path& crtFile, const fs::path& keyFile, EVP_PKEY* pKey, X509** outCert) {
    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating CA certificate...");

    *outCert = X509_new();

    if (*outCert == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The CA certificate couldn't be generated: " +
                                                                  util::getOpenSSLError());
        return false;
    }

    X509_set_version(*outCert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(*outCert), 1);
    X509_gmtime_adj(X509_get_notBefore(*outCert), 0);
    X509_gmtime_adj(X509_get_notAfter(*outCert), 315360000L); // 10 years aprox.
    X509_set_pubkey(*outCert, pKey);

    X509_NAME* certName = X509_get_subject_name(*outCert);
    X509_NAME_add_entry_by_txt(certName, "CN", MBSTRING_ASC, (unsigned char *) "SplatIt Server CA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "O", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "C", MBSTRING_ASC, (unsigned char *) "ES", -1, -1, 0);

    X509_set_issuer_name(*outCert, certName);

    try {
        addExtToCert(*outCert, *outCert, NID_subject_key_identifier, "hash");
        addExtToCert(*outCert, *outCert, NID_authority_key_identifier, "keyid:always,issuer:always");
        addExtToCert(*outCert, *outCert, NID_basic_constraints, "CA:TRUE");
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The CA certificate couldn't be generated: " + std::string(ex.what()));
        X509_free(*outCert);
        return false;
    }

    if (X509_sign(*outCert, pKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The CA certificate couldn't be signed: " +
                                                                  util::getOpenSSLError());
        X509_free(*outCert);
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Writing CA certificate and key to disk...");

    if(!writeKey(pKey, keyFile) || !writeCert(*outCert, crtFile)) {
        X509_free(*outCert);
        return false;
    }

    return true;
}

bool CertManager::createSSLServerCert(X509* caCert, EVP_PKEY* caKey, const fs::path& crtFile,
                                      const fs::path& keyFile, const std::vector<std::string>& domains,
                                      EVP_PKEY* pKey, X509** outCert) {
    assert(!domains.empty());

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating server certificate...");

    *outCert = X509_new();

    if (*outCert == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The server certificate couldn't be generated: " +
                                                                  util::getOpenSSLError());
        return false;
    }

    X509_set_version(*outCert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(*outCert), 0x495);
    X509_gmtime_adj(X509_get_notBefore(*outCert), 0);
    X509_gmtime_adj(X509_get_notAfter(*outCert), 157680000L); // 5 years aprox.
    X509_set_pubkey(*outCert, pKey);

    X509_NAME* certName = X509_get_subject_name(*outCert);
    X509_NAME_add_entry_by_txt(certName, "CN", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "O", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "C", MBSTRING_ASC, (unsigned char *) "ES", -1, -1, 0);

    X509_set_subject_name(*outCert, certName);
    X509_set_issuer_name(*outCert, X509_get_subject_name(caCert));

    try {
        addExtToCert(caCert, *outCert, NID_basic_constraints, "CA:FALSE");
        addExtToCert(caCert, *outCert, NID_subject_key_identifier, "hash");
        addExtToCert(caCert, *outCert, NID_authority_key_identifier, "keyid:always,issuer:always");
        addExtToCert(caCert, *outCert, NID_ext_key_usage, "serverAuth");
        addExtToCert(caCert, *outCert, NID_key_usage, "digitalSignature,keyEncipherment");

        std::string cert_SAN;
        for (const std::string& domain : domains) {
            cert_SAN += "DNS:" + domain + ",";
        }
        cert_SAN.pop_back();

        addExtToCert(caCert, *outCert, NID_subject_alt_name, cert_SAN);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The server certificate couldn't be generated: " + std::string(ex.what()));
        X509_free(*outCert);
        return false;
    }

    if (X509_sign(*outCert, caKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::SETUP, "The server certificate couldn't be signed: " +
                                                                  util::getOpenSSLError());
        X509_free(*outCert);
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Writing server certificate and key to disk...");

    if(!writeKey(pKey, keyFile) || !writeCert(*outCert, crtFile)) {
        X509_free(*outCert);
        return false;
    }

    return true;
}

void setU32OnBinary(std::vector<uint8_t>& vec, const uint32_t val, const size_t offset) {
    vec[offset] = static_cast<uint8_t>((val >> 24) & 0xFF);
    vec[offset + 1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    vec[offset + 2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    vec[offset + 3] = static_cast<uint8_t>(val & 0xFF);
}

void setU32OnBinaryBigEndian(std::vector<uint8_t>& vec, const uint32_t val, const size_t offset) {
    vec[offset] = static_cast<uint8_t>(val & 0xFF);
    vec[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    vec[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    vec[offset + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

DeviceCemuFiles CertManager::genDeviceCemuFiles(uint32_t deviceId, uint8_t region, const std::string& serialNumber) {
    if (CAkey == nullptr) throw CAKeyNotProvidedException();

    // --- Certificate and key generation ---
    EVP_PKEY* clientKey = nullptr;
    if (!genRSAKey(&clientKey, Logger::group::ACCOUNT)) {
        throw std::runtime_error("Failed to generate client key");
    }

    std::string certCommonName = "SplatIt Wii U Common " + std::to_string(deviceId);
    X509* clientCert = nullptr;
    if (!createClientCert(CAcert, CAkey, certCommonName, clientKey, &clientCert)) {
        throw std::runtime_error("Failed to create client certificate");
    }

    // Convert client certificate to DER format
    int certDerLen = i2d_X509(clientCert, nullptr);
    if (certDerLen < 0) {
        EVP_PKEY_free(clientKey);
        X509_free(clientCert);
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Failed to get client certificate DER length: " +
                                                                  util::getOpenSSLError());
        throw std::runtime_error("Failed to get client certificate DER length");
    }

    std::vector<uint8_t> clientCertDer(certDerLen);
    unsigned char* certPtr = clientCertDer.data();
    if (i2d_X509(clientCert, &certPtr) < 0) {
        EVP_PKEY_free(clientKey);
        X509_free(clientCert);
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Failed to convert client certificate to DER: " +
                                                                  util::getOpenSSLError());
        throw std::runtime_error("Failed to convert client certificate to DER");
    }

    // Convert client key to DER format (PKCS#8)
    int keyDerLen = i2d_PrivateKey(clientKey, nullptr);
    if (keyDerLen < 0) {
        EVP_PKEY_free(clientKey);
        X509_free(clientCert);
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Failed to get client key DER length: " +
                                                                  util::getOpenSSLError());
        throw std::runtime_error("Failed to get client key DER length");
    }

    std::vector<uint8_t> clientKeyDer(keyDerLen);
    unsigned char* keyPtr = clientKeyDer.data();
    if (i2d_PrivateKey(clientKey, &keyPtr) < 0) {
        EVP_PKEY_free(clientKey);
        X509_free(clientCert);
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Failed to convert client key to DER: " +
                                                                  util::getOpenSSLError());
        throw std::runtime_error("Failed to convert client key to DER");
    }

    // Convert CA certificate to DER format
    int caCertDerLen = i2d_X509(CAcert, nullptr);
    if (caCertDerLen < 0) {
        EVP_PKEY_free(clientKey);
        X509_free(clientCert);
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Failed to get CA certificate DER length: " +
                                                                  util::getOpenSSLError());
        throw std::runtime_error("Failed to get CA certificate DER length");
    }

    std::vector<uint8_t> caCertDer(caCertDerLen);
    unsigned char* caCertPtr = caCertDer.data();
    if (i2d_X509(CAcert, &caCertPtr) < 0) {
        EVP_PKEY_free(clientKey);
        X509_free(clientCert);
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "Failed to convert CA certificate to DER: " +
                                                                  util::getOpenSSLError());
        throw std::runtime_error("Failed to convert CA certificate to DER");
    }

    // Generate random AES-128 key for client key encryption
    std::vector<uint8_t> aesKey = genKey(16);

    // Pad clientKeyDer to a multiple of 16 bytes filling with zeros
    size_t originalKeySize = clientKeyDer.size();
    size_t paddedKeySize = ((originalKeySize + 15) / 16) * 16;
    clientKeyDer.resize(paddedKeySize, 0);

    // Encrypt client key using AES-128 CBC with IV of zeros
    std::vector<uint8_t> iv(16, 0); // Initialization vector of zeros
    std::vector<uint8_t> encryptedClientKeyDer = AES_128_CBC_ENC(aesKey, iv, clientKeyDer);

    // Clean up OpenSSL objects
    EVP_PKEY_free(clientKey);
    X509_free(clientCert);

    DeviceCemuFiles cemuFiles;
    cemuFiles.clientCert = std::move(clientCertDer);
    cemuFiles.clientKey = std::move(encryptedClientKeyDer);
    cemuFiles.serverCA = std::move(caCertDer);

    // --- OTP Generation ---
    std::vector<uint8_t> otp(0x400, 0);

    for (uint8_t i = 0; i < aesKey.size(); i++) otp[0x120 + i] = aesKey[i]; // Write AES key at 0x120
    setU32OnBinary(otp, deviceId, 0x21C); // Write Device ID at 0x21C

    uint32_t caId = 0x00000003;
    uint32_t msId = 0x00000012;

    setU32OnBinary(otp, caId, 0x284); // Write CA ID at 0x284
    setU32OnBinary(otp, msId, 0x280); // Write MS ID at 0x280

    std::vector<uint8_t> ngKeyIdVec = genKey(4);
    uint32_t ngKeyId = (static_cast<uint32_t>(ngKeyIdVec[0]) << 24) |
                           (static_cast<uint32_t>(ngKeyIdVec[1]) << 16) |
                           (static_cast<uint32_t>(ngKeyIdVec[2]) << 8) |
                           static_cast<uint32_t>(ngKeyIdVec[3]);
    setU32OnBinary(otp, ngKeyId, 0x288); // Write NG Key ID at 0x288

    // --- Device certificate generation ---
    EVP_PKEY* deviceCertKey = nullptr;
    if (!genECDSAKey(&deviceCertKey, Logger::group::ACCOUNT)) {
        throw std::runtime_error("Failed to generate device certificate key");
    }

    // Get private value and write it to OTP at 0x220
    BIGNUM* privKeyBN = nullptr;
    if (!EVP_PKEY_get_bn_param(deviceCertKey, OSSL_PKEY_PARAM_PRIV_KEY, &privKeyBN)) {
        EVP_PKEY_free(deviceCertKey);
        throw std::runtime_error("Failed to get private key parameter");
    }
    BN_bn2binpad(privKeyBN, otp.data() + 0x220, 30); // sect233r1 private key size (233 bits = 30 bytes)
    BN_free(privKeyBN);

    std::vector<uint8_t> deviceCert(0x100);
    snprintf(reinterpret_cast<char*>(deviceCert.data()), deviceCert.size(), "Root-CA%08x-MS%08x", caId, msId);
    setU32OnBinaryBigEndian(deviceCert, 2, 0x40); // Key type (always 2)
    snprintf(reinterpret_cast<char*>(deviceCert.data() + 0x44), deviceCert.size() - 0x44, "NG%08x", deviceId);
    setU32OnBinaryBigEndian(deviceCert, ngKeyId, 0x80); // NG Key ID

    // Get public key point coordinates
    BIGNUM* pubKeyX = nullptr;
    BIGNUM* pubKeyY = nullptr;
    if (!EVP_PKEY_get_bn_param(deviceCertKey, OSSL_PKEY_PARAM_EC_PUB_X, &pubKeyX) ||
        !EVP_PKEY_get_bn_param(deviceCertKey, OSSL_PKEY_PARAM_EC_PUB_Y, &pubKeyY)) {
        EVP_PKEY_free(deviceCertKey);
        BN_free(pubKeyX);
        throw std::runtime_error("Failed to get public key coordinates");
    }

    // Write X coordinate to deviceCert at 0x84 (30 bytes for sect233r1)
    BN_bn2binpad(pubKeyX, deviceCert.data() + 0x84, 30);

    // Write Y coordinate to deviceCert at 0xA0 (30 bytes for sect233r1)
    BN_bn2binpad(pubKeyY, deviceCert.data() + 0xA0, 30);

    BN_free(pubKeyX);
    BN_free(pubKeyY);

    EVP_PKEY_free(deviceCertKey);

    // Sign device certificate with device key using SHA256
    EVP_MD_CTX* signCtx = EVP_MD_CTX_new();
    if (!signCtx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX for signing: " + util::getOpenSSLError());
    }

    if (EVP_DigestSignInit(signCtx, nullptr, EVP_sha256(), nullptr, this->deviceKey) != 1) {
        EVP_MD_CTX_free(signCtx);
        throw std::runtime_error("Failed to initialize EVP_MD_CTX for signing: " + util::getOpenSSLError());
    }

    size_t sigLen = 0;
    if (EVP_DigestSign(signCtx, nullptr, &sigLen, deviceCert.data(), deviceCert.size()) != 1) {
        EVP_MD_CTX_free(signCtx);
        throw std::runtime_error("Failed to get signature length: " + util::getOpenSSLError());
    }

    std::vector<uint8_t> derSignature(sigLen);
    if (EVP_DigestSign(signCtx, derSignature.data(), &sigLen, deviceCert.data(), deviceCert.size()) != 1) {
        EVP_MD_CTX_free(signCtx);
        throw std::runtime_error("Failed to sign device certificate: " + util::getOpenSSLError());
    }
    derSignature.resize(sigLen);
    EVP_MD_CTX_free(signCtx);

    // Convert DER signature to raw format (r and s components)
    const unsigned char* derPtr = derSignature.data();
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &derPtr, static_cast<long>(derSignature.size()));
    if (!sig) {
        throw std::runtime_error("Failed to parse DER signature: " + util::getOpenSSLError());
    }

    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig, &r, &s);

    if (!r || !s) {
        ECDSA_SIG_free(sig);
        throw std::runtime_error("Failed to get signature components");
    }

    // Convert r and s to binary (30 bytes each for sect233r1) and write to OTP at 0x28C
    BN_bn2binpad(r, otp.data() + 0x28C, 30);
    BN_bn2binpad(s, otp.data() + 0x28C + 30, 30);

    ECDSA_SIG_free(sig);

    cemuFiles.otp = std::move(otp);

    // --- Seeprom generation ---
    std::vector<uint8_t> seeprom(0x200, 0);

    // Validate and parse the serial number
    std::regex serialRegex("^([FG][EJW][FHM]?)(\\d{9})$");
    std::smatch matches;

    if (!std::regex_match(serialNumber, matches, serialRegex)) {
        throw std::invalid_argument("Invalid serial number format");
    }

    std::string serialLetters = matches[1].str();
    std::string serialNumbers = matches[2].str();

    setU32OnBinary(seeprom, region, 0x148); // Write region at 0x148

    // Write serial number letters at 0x158
    for (size_t i = 0; i < serialLetters.size(); i++) seeprom[0x158 + i] = static_cast<uint8_t>(serialLetters[i]);

    // Write serial number digits at 0x160
    for (size_t i = 0; i < serialNumbers.size(); i++) seeprom[0x160 + i] = static_cast<uint8_t>(serialNumbers[i]);

    cemuFiles.seeprom = std::move(seeprom);

    return cemuFiles;
}

bool CertManager::createClientCert(X509* caCert, EVP_PKEY* caKey, const std::string& commonName,
                                   EVP_PKEY* pKey, X509** outCert) {
    logger->log(Logger::level::INFO, Logger::group::ACCOUNT, "Creating client certificate...");

    *outCert = X509_new();

    if (*outCert == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "The client certificate couldn't be generated: " +
                                                                  util::getOpenSSLError());
        return false;
    }

    X509_set_version(*outCert, 2);

    // Generate a random high serial number
    BIGNUM* serialBN = BN_new();
    if (serialBN == nullptr) {
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "The client certificate couldn't be generated: " +
                                                                  util::getOpenSSLError());
        X509_free(*outCert);
        return false;
    }

    if (BN_rand(serialBN, 64, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) != 1) {
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "The client certificate couldn't be generated: " +
                                                                  util::getOpenSSLError());
        BN_free(serialBN);
        X509_free(*outCert);
        return false;
    }

    ASN1_INTEGER* serial = X509_get_serialNumber(*outCert);
    BN_to_ASN1_INTEGER(serialBN, serial);
    BN_free(serialBN);

    X509_gmtime_adj(X509_get_notBefore(*outCert), 0);
    X509_gmtime_adj(X509_get_notAfter(*outCert), 157680000L); // 5 years aprox.
    X509_set_pubkey(*outCert, pKey);

    X509_NAME* certName = X509_get_subject_name(*outCert);
    X509_NAME_add_entry_by_txt(certName, "CN", MBSTRING_ASC, (unsigned char *) commonName.c_str(), -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "O", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "C", MBSTRING_ASC, (unsigned char *) "ES", -1, -1, 0);

    X509_set_subject_name(*outCert, certName);
    X509_set_issuer_name(*outCert, X509_get_subject_name(caCert));

    try {
        addExtToCert(caCert, *outCert, NID_basic_constraints, "CA:FALSE");
        addExtToCert(caCert, *outCert, NID_subject_key_identifier, "hash");
        addExtToCert(caCert, *outCert, NID_authority_key_identifier, "keyid:always,issuer:always");
    } catch (const std::exception& ex) {
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "The client certificate couldn't be generated: " + std::string(ex.what()));
        X509_free(*outCert);
        return false;
    }

    if (X509_sign(*outCert, caKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::FAILURE, Logger::group::ACCOUNT, "The client certificate couldn't be signed: " +
                                                                  util::getOpenSSLError());
        X509_free(*outCert);
        return false;
    }

    return true;
}

void CertManager::addExtToCert(X509* ca, X509* cert, int nid, const std::string& value) {
    X509_EXTENSION* ext;
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx)
    X509V3_set_ctx(&ctx, ca, cert, nullptr, nullptr, 0);
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.c_str());

    if (!ext) throw std::runtime_error(util::getOpenSSLError());

    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);
}

EVP_PKEY* CertManager::getSSLKey() {
    return key;
}

X509* CertManager::getSSLCert() {
    return cert;
}

EVP_PKEY* CertManager::getManagementSSLKey() {
    return managementKey;
}

X509* CertManager::getManagementSSLCert() {
    return managementCert;
}

EVP_PKEY* CertManager::getDeviceKey() {
    return deviceKey;
}

} // namespace crypto