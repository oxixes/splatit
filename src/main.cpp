#include "argParser.hpp"

int main(int argc, char** argv) {
    Logger::Logger logger;

    argParser::options serverOptions{};
    if (!argParser::parseArgs(argc, argv, serverOptions, &logger)) return 0;

    logger.setMinLevel(serverOptions.minLogLevel);

    logger.log(Logger::level::INFO, Logger::group::SETUP, "Data path: " + serverOptions.data_path + "\n");

    return 0;
}
