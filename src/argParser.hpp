#ifndef SPLATOON_SERVER_ARGPARSER_HPP
#define SPLATOON_SERVER_ARGPARSER_HPP

#include <getopt.h>
#include <iostream>
#include <memory>

#include "logger.hpp"

namespace argParser {

    struct options {
        bool no_account;
        bool no_boss;
        bool no_friends_auth;
        bool no_friends_secure;
        bool no_splatoon_auth;
        bool no_splatoon_secure;

        std::string data_path = "data";
        Logger::level minLogLevel = Logger::level::INFO;
    };

    bool parseArgs(int argc, char** argv, options& serverOptions, const std::shared_ptr<Logger::Logger> &logger);

    void printHelp(const char* argv0);

} // namespace argParser

#endif //SPLATOON_SERVER_ARGPARSER_HPP
