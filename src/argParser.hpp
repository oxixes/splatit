#ifndef SPLATOON_SERVER_ARGPARSER_HPP
#define SPLATOON_SERVER_ARGPARSER_HPP

#include <iostream>
#include <memory>
#include <string>

#include "logger.hpp"

namespace argParser {

    struct options {
        std::string data_path = "data";
        Logger::level minLogLevel = Logger::level::INFO;
    };

    bool parseArgs(int argc, char** argv, options& serverOptions, const std::shared_ptr<Logger::Logger>& logger);

    void printHelp(const char* argv0);

} // namespace argParser

#endif //SPLATOON_SERVER_ARGPARSER_HPP
