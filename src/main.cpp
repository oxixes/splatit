#include "argParser.hpp"
#include "settingsManager.hpp"

int main(int argc, char** argv) {
    Logger::Logger logger;

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, &logger)) return 0;

    logger.setMinLevel(serverOptions.minLogLevel);

    SettingsManager settingsMgr = SettingsManager(&logger);
    settingsMgr.init(serverOptions);

    return 0;
}
