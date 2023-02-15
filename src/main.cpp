#include "argParser.hpp"
#include "settingsManager.hpp"
#include "ssl/certManager.hpp"

int main(int argc, char** argv) {
    // Init OpenSSL
    OpenSSL_add_all_algorithms();

    Logger::Logger logger;

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, &logger)) return 1;

    logger.setMinLevel(serverOptions.minLogLevel);

    SettingsManager settingsMgr = SettingsManager(&logger);
    if (!settingsMgr.init(serverOptions)) return 1;

    EVP_PKEY* CAkey;
    X509* CAcert;
    EVP_PKEY* key;
    X509* cert;
    CertManager certManager = CertManager(&settingsMgr, &logger);
    //certManager.createCA("data", "ca", &CAkey, &CAcert);
    //certManager.createSSLServerCert(CAcert, CAkey, "data", "any.nintendo.net",
    //                                std::vector<std::string>{"account.nintendo.net", "boss.cdn.nintendo.net"}, &key,
    //                                &cert);

    certManager.loadKey("data/ca.key", &CAkey);
    certManager.loadKey("data/any.nintendo.net.key", &key);
    certManager.loadCert("data/ca.crt", &CAcert);
    certManager.loadCert("data/any.nintendo.net.crt", &cert);

    return 0;
}
