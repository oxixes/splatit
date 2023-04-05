#include "certManager.hpp"

CertManager::CertManager(SettingsManager* settingsManager, Logger::Logger* logger) {
    this->settingsManager = settingsManager;
    this->logger = logger;

    this->CAkey = nullptr;
    this->CAcert = nullptr;
    this->key = nullptr;
    this->cert = nullptr;
}

CertManager::~CertManager() {
    if (CAkey) EVP_PKEY_free(CAkey);
    if (CAcert) X509_free(CAcert);
    if (key) EVP_PKEY_free(key);
    if (cert) X509_free(cert);
}

bool CertManager::init() {
    if (!util::checkParentDirectory(settingsManager->getSSLCAKeyPath())) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
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

    if (!util::checkParentDirectory(settingsManager->getSSLCACertPath())) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    settingsManager->getSSLCAKeyPath().parent_path().string() + " is not a directory!");
        return false;
    }

    if (fs::exists(settingsManager->getSSLCACertPath())) {
        logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading CA cert...");
        if (!loadCert(settingsManager->getSSLCACertPath(), &CAcert)) return false;
        if (!validateCA(CAcert, CAkey)) return false;
    } else {
        logger->log(Logger::level::INFO, Logger::group::SETUP, "CA cert not found!");
        if (!createCA(settingsManager->getSSLCACertPath(),settingsManager->getSSLCAKeyPath(),
                      CAkey, &CAcert)) return false;
    }

    if (!util::checkParentDirectory(settingsManager->getSSLKeyPath())) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    settingsManager->getSSLCAKeyPath().parent_path().string() + " is not a directory!");
        return false;
    }

    if (fs::exists(settingsManager->getSSLKeyPath())) {
        logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading key...");
        if (!loadKey(settingsManager->getSSLKeyPath(), &key)) return false;
        if (!validateRSAKey(CAkey)) return false;
    } else {
        logger->log(Logger::level::INFO, Logger::group::SETUP, "Key not found!");
        if (!genRSAKey(&key)) return false;
    }

    if (!util::checkParentDirectory(settingsManager->getSSLCertPath())) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    settingsManager->getSSLCAKeyPath().parent_path().string() + " is not a directory!");
        return false;
    }

    if (fs::exists(settingsManager->getSSLCertPath())) {
        logger->log(Logger::level::INFO, Logger::group::SETUP, "Loading SSL certificate...");
        if (!loadCert(settingsManager->getSSLCertPath(), &cert)) return false;
        if (!validateSSLCert(cert, key, CAcert, settingsManager->getDomains())) return false;
    } else {
        logger->log(Logger::level::INFO, Logger::group::SETUP, "Cert not found!");
        if (!createSSLServerCert(CAcert, CAkey, settingsManager->getSSLCertPath(),
                                 settingsManager->getSSLKeyPath(), settingsManager->getDomains(),
                                 key, &cert)) return false;
    }

    return true;
}

bool CertManager::loadKey(const fs::path& keyPath, EVP_PKEY** pKey) {
    FILE* pKeyFile = fopen(keyPath.string().c_str(), "rb");

    if (pKeyFile == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The private key couldn't be loaded: " +
                                                                std::string(strerror(errno)));
        return false;
    }

    *pKey = PEM_read_PrivateKey(pKeyFile, nullptr, nullptr, nullptr);
    if (*pKey == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The private key couldn't be loaded: " +
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
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be loaded: " +
                                                                std::string(strerror(errno)));
        return false;
    }

    *outCert = PEM_read_X509(certFile, nullptr, nullptr, nullptr);
    if (*outCert == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be loaded: " +
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
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The private key couldn't be written to disk: " +
                                                                std::string(strerror(errno)));
        return false;
    }

    if (PEM_write_PrivateKey(pKeyFile, pKey, nullptr, nullptr, 0, nullptr, nullptr) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The private key couldn't be written to disk: " +
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
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be written to disk: " +
                                                                std::string(strerror(errno)));
        return false;
    }

    if (PEM_write_X509(certFile, crt) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be written to disk: " +
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
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate is not a CA!");
        return false;
    }

    return true;
}

bool CertManager::validateSSLCert(X509* crt, EVP_PKEY* pKey, X509* CAcrt, const std::vector<std::string>& domains) {
    if (!validateCert(crt, pKey)) return false;

    // Check if the certificate is signed by the CA
    X509_STORE* store = X509_STORE_new();
    if(store == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be validated: " +
                util::getOpenSSLError());
        return false;
    }

    if(X509_STORE_add_cert(store, CAcrt) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be validated: " +
                util::getOpenSSLError());
        return false;
    }

    X509_STORE_CTX* storeCtx = X509_STORE_CTX_new();
    if(storeCtx == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be validated: " +
                util::getOpenSSLError());
        return false;
    }

    if (X509_STORE_CTX_init(storeCtx, store, crt, nullptr) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be validated: " +
                util::getOpenSSLError());
        return false;
    }

    if (X509_verify_cert(storeCtx) != 1) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate is not signed by the CA or hasn't been issued correctly!");
        return false;
    }

    X509_STORE_CTX_free(storeCtx);
    X509_STORE_free(store);

    // Check if the certificate is a CA certificate
    if (X509_check_ca(crt) != 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate is a CA certificate!");
        return false;
    }

    // Check if the certificate is valid for the given domains
    std::vector<std::string> certDomains{};

    int extLoc = X509_get_ext_by_NID(crt, NID_subject_alt_name, -1);
    X509_EXTENSION* ext = X509_get_ext(crt, extLoc);
    if (ext == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
                    "The certificate doesn't contain the subject alternative name extension!");
        return false;
    }

    STACK_OF(GENERAL_NAME)* domainsList = (STACK_OF(GENERAL_NAME)*) X509V3_EXT_d2i(ext);
    if (domainsList == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP,
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
            logger->log(Logger::level::ERROR, Logger::group::SETUP,
                        "The certificate doesn't contain the domain " + domain + ".");
            invalid = true;
        }
    }

    sk_GENERAL_NAME_pop_free(domainsList, GENERAL_NAME_free);

    return !invalid;
}

bool CertManager::validateCert(X509* crt, EVP_PKEY* pKey) {
    if (!validateRSAKey(pKey)) return false;

    if (X509_check_private_key(crt, pKey) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate and the key don't match!");
        return false;
    }

    if (X509_cmp_current_time(X509_get_notBefore(crt)) >= 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate is not valid yet!");
        return false;
    }

    if (X509_cmp_current_time(X509_get_notAfter(crt)) <= 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate is expired!");
        return false;
    }

    return true;
}

bool CertManager::validateRSAKey(EVP_PKEY* pKey) {
    int keyType = EVP_PKEY_type(EVP_PKEY_get_id(pKey));

    if (keyType != EVP_PKEY_RSA && keyType != EVP_PKEY_RSA2) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The key is not an RSA key!");
        return false;
    }

    return true;
}

bool CertManager::genRSAKey(EVP_PKEY** pKey) {
    logger->log(Logger::level::INFO, Logger::group::SETUP, "Generating RSA private key...");
    *pKey = EVP_RSA_gen(2048);

    if (*pKey == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The RSA key couldn't be generated: " +
                util::getOpenSSLError());
        return false;
    }

    return true;
}

bool CertManager::createCA(const fs::path& crtFile, const fs::path& keyFile, EVP_PKEY* pKey, X509** outCert) {
    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating CA certificate...");

    *outCert = X509_new();

    if (*outCert == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be generated: " +
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
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be generated: " + std::string(ex.what()));
        X509_free(*outCert);
        return false;
    }

    if (X509_sign(*outCert, pKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be signed: " +
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
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The server certificate couldn't be generated: " +
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
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The server certificate couldn't be generated: " + std::string(ex.what()));
        X509_free(*outCert);
        return false;
    }

    if (X509_sign(*outCert, caKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The server certificate couldn't be signed: " +
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