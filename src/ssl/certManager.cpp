#include "certManager.hpp"

CertManager::CertManager(SettingsManager* settingsManager, Logger::Logger* logger) {
    this->settingsManager = settingsManager;
    this->logger = logger;
}

bool CertManager::createCA(const fs::path& path, const std::string& filename, EVP_PKEY* privKey, X509* CAcert) {
    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating RSA private key for CA...");
    EVP_PKEY* pKey = EVP_RSA_gen(2048);

    if (pKey == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The RSA key couldn't be generated: " +
                getOpenSSLerror());
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Creating CA cert...");
    X509* cert = X509_new();

    if (cert == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be generated: " +
                getOpenSSLerror());
        EVP_PKEY_free(pKey);
        return false;
    }

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 315360000L); // 10 years aprox.
    X509_set_pubkey(cert, pKey);

    X509_NAME* certName = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(certName, "CN", MBSTRING_ASC, (unsigned char *) "SplatIt Server CA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "O", MBSTRING_ASC, (unsigned char *) "SplatIt Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(certName, "C", MBSTRING_ASC, (unsigned char *) "ES", -1, -1, 0);

    X509_set_issuer_name(cert, certName);

    try {
        addExtToCert(cert, NID_subject_key_identifier, "hash");
        addExtToCert(cert, NID_authority_key_identifier, "keyid:always,issuer:always");
        addExtToCert(cert, NID_basic_constraints, "CA:TRUE");
    } catch (const std::exception& ex) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be signed: " + std::string(ex.what()));
        return false;
    }

    if (X509_sign(cert, pKey, EVP_sha256()) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be signed: " +
                getOpenSSLerror());
        X509_free(cert);
        EVP_PKEY_free(pKey);
        return false;
    }

    logger->log(Logger::level::INFO, Logger::group::SETUP, "Writing CA cert and key to disk...");
    FILE* pKeyFile = fopen((path/(filename + ".key")).string().c_str(), "wb");

    if (pKeyFile == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA private key couldn't be written to disk: " +
                std::string(strerror(errno)));
        X509_free(cert);
        EVP_PKEY_free(pKey);
        return false;
    }

    if (PEM_write_PrivateKey(pKeyFile, pKey, nullptr, nullptr, 0, nullptr, nullptr) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA private key couldn't be written to disk: " +
                getOpenSSLerror());
        X509_free(cert);
        EVP_PKEY_free(pKey);
        return false;
    }

    FILE* certFile = fopen((path/(filename + ".crt")).string().c_str(), "wb");

    if (certFile == nullptr) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be written to disk: " +
                std::string(strerror(errno)));
        X509_free(cert);
        EVP_PKEY_free(pKey);
        return false;
    }

    if (PEM_write_X509(certFile, cert) == 0) {
        logger->log(Logger::level::ERROR, Logger::group::SETUP, "The CA certificate couldn't be written to disk: " +
                getOpenSSLerror());
        X509_free(cert);
        EVP_PKEY_free(pKey);
        return false;
    }

    privKey = pKey;
    CAcert = cert;
    return true;
}

bool CertManager::createSSLCert(const std::string& caCertPath, const std::string& caKeyPath,
                                const std::string& path, const std::string& filename,
                                const std::vector<std::string>& domains) {
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