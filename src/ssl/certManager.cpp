// FIXME: Reduce code duplication in createCA and createSSLCert

#include "certManager.hpp"

CertManager::CertManager(SettingsManager* settingsManager, Logger::Logger* logger) {
    this->settingsManager = settingsManager;
    this->logger = logger;
}

bool CertManager::createCA(const fs::path& path, const std::string& filename, EVP_PKEY** pKey, X509** cert) {
    logger->log(Logger::level::INFO, Logger::group::SETUP, "Generating RSA private key for CA...");
    *pKey = EVP_RSA_gen(2048);

    if (*pKey == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The RSA key couldn't be generated: " +
                getOpenSSLerror());
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating CA certificate...");
    *cert = X509_new();

    if (*cert == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be generated: " +
                getOpenSSLerror());
        EVP_PKEY_free(*pKey);
        return false;
    }

    X509_set_version(*cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(*cert), 1);
    X509_gmtime_adj(X509_get_notBefore(*cert), 0);
    X509_gmtime_adj(X509_get_notAfter(*cert), 315360000L); // 10 years aprox.
    X509_set_pubkey(*cert, *pKey);

    X509_NAME* certName = X509_get_subject_name(*cert);
    X509_NAME_add_entry_by_txt(certName, "CN", MBSTRING_ASC, (unsigned char *) "SplatIt Server CA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "O", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "C", MBSTRING_ASC, (unsigned char *) "ES", -1, -1, 0);

    X509_set_issuer_name(*cert, certName);

    try {
        addExtToCert(*cert, NID_subject_key_identifier, "hash");
        addExtToCert(*cert, NID_authority_key_identifier, "keyid:always,issuer:always");
        addExtToCert(*cert, NID_basic_constraints, "CA:TRUE");
    } catch (const std::exception& ex) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be generated: " + std::string(ex.what()));
        X509_free(*cert);
        EVP_PKEY_free(*pKey);
        return false;
    }

    if (X509_sign(*cert, *pKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be signed: " +
                getOpenSSLerror());
        X509_free(*cert);
        EVP_PKEY_free(*pKey);
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Writing CA certificate and key to disk...");

    if(!writeKeyAndCertToDisk(pKey, cert, path / (filename + ".key"), path / (filename + ".crt"))) {
        X509_free(*cert);
        EVP_PKEY_free(*pKey);
        return false;
    }

    return true;
}

bool CertManager::createSSLCert(X509** caCert, EVP_PKEY** caKey, const fs::path& path, const std::string& filename,
                                const std::vector<std::string>& domains, EVP_PKEY** pKey, X509** cert) {
    assert(!domains.empty());

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Generating RSA private key for server certificate...");
    *pKey = EVP_RSA_gen(2048);

    if (*pKey == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The RSA key couldn't be generated: " +
                                                                getOpenSSLerror());
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating server certificate...");
    *cert = X509_new();

    if (*cert == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The server certificate couldn't be generated: " +
                                                                getOpenSSLerror());
        EVP_PKEY_free(*pKey);
        return false;
    }

    X509_set_version(*cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(*cert), 0x495);
    X509_gmtime_adj(X509_get_notBefore(*cert), 0);
    X509_gmtime_adj(X509_get_notAfter(*cert), 157680000L); // 5 years aprox.
    X509_set_pubkey(*cert, *pKey);

    X509_NAME* certName = X509_get_subject_name(*cert);
    X509_NAME_add_entry_by_txt(certName, "CN", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "O", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "C", MBSTRING_ASC, (unsigned char *) "ES", -1, -1, 0);

    X509_set_subject_name(*cert, certName);
    X509_set_issuer_name(*cert, X509_get_subject_name(*caCert));

    try {
        addExtToCert(*cert, NID_basic_constraints, "CA:FALSE");
        addExtToCert(*cert, NID_subject_key_identifier, "hash");
        addExtToCert(*cert, NID_authority_key_identifier, "keyid:always,issuer:always");
        addExtToCert(*cert, NID_ext_key_usage, "serverAuth");
        addExtToCert(*cert, NID_key_usage, "digitalSignature,keyEncipherment");

        std::string cert_SAN;
        for (const std::string& domain : domains) {
            cert_SAN += "DNS:" + domain + ",";
        }
        cert_SAN.pop_back();

        addExtToCert(*cert, NID_subject_alt_name, cert_SAN);
    } catch (const std::exception& ex) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The server certificate couldn't be generated: " + std::string(ex.what()));
        X509_free(*cert);
        EVP_PKEY_free(*pKey);
        return false;
    }

    if (X509_sign(*cert, *caKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The server certificate couldn't be signed: " +
                                                                getOpenSSLerror());
        X509_free(*cert);
        EVP_PKEY_free(*pKey);
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Writing server certificate and key to disk...");

    if(!writeKeyAndCertToDisk(pKey, cert, path / (filename + ".key"), path / (filename + ".crt"))) {
        X509_free(*cert);
        EVP_PKEY_free(*pKey);
        return false;
    }

    return true;
}

bool CertManager::writeKeyAndCertToDisk(EVP_PKEY** pKey, X509** cert, const fs::path& keyPath, const fs::path& certPath) {
    FILE* pKeyFile = fopen(keyPath.string().c_str(), "wb");

    if (pKeyFile == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The private key couldn't be written to disk: " +
                                                                std::string(strerror(errno)));
        return false;
    }

    if (PEM_write_PrivateKey(pKeyFile, *pKey, nullptr, nullptr, 0, nullptr, nullptr) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The private key couldn't be written to disk: " +
                                                                getOpenSSLerror());
        return false;
    }

    FILE* certFile = fopen(certPath.string().c_str(), "wb");

    if (certFile == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be written to disk: " +
                                                                std::string(strerror(errno)));
        return false;
    }

    if (PEM_write_X509(certFile, *cert) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The certificate couldn't be written to disk: " +
                                                                getOpenSSLerror());
        return false;
    }

    return true;
}

void CertManager::addExtToCert(X509* cert, int nid, const std::string& value) {
    X509_EXTENSION* ext;
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.c_str());

    if (!ext) throw std::runtime_error(getOpenSSLerror());

    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);
}

std::string CertManager::getOpenSSLerror() {
    char buff[512];
    ERR_error_string(ERR_get_error(), buff);
    return std::string{buff};
}