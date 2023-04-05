#include "argParser.hpp"
#include "settingsManager.hpp"
#include "ssl/certManager.hpp"
#include "http/server.hpp"

int main(int argc, char** argv) {
    // Init OpenSSL
    OpenSSL_add_all_algorithms();

    Logger::Logger logger;

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, &logger)) return 1;

    logger.setMinLevel(serverOptions.minLogLevel);

    SettingsManager settingsMgr = SettingsManager(&logger);
    if (!settingsMgr.init(serverOptions)) return 1;

    CertManager certManager = CertManager(&settingsMgr, &logger);
    if (settingsMgr.isAccountEnabled() || settingsMgr.isBOSSEnabled()) {
        // TODO Cleanup
        if (!certManager.init()) return 1;

        HTTP_Server httpServer = HTTP_Server(&logger, &settingsMgr, &certManager);
        httpServer.listen();
    }

    return 0;
}
