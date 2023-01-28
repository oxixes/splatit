#ifndef SPLATOON_SERVER_ARGPARSER_HPP
#define SPLATOON_SERVER_ARGPARSER_HPP

#include <getopt.h>
#include <iostream>

namespace argParser {

    struct options {
        bool no_account;
        bool no_boss;
        bool no_friends_auth;
        bool no_friends_secure;
        bool no_splatoon_auth;
        bool no_splatoon_secure;
    };

    bool parseArgs(int argc, char** argv, options& serverOptions);

    void printHelp(const char* argv0);

} // namespace argParser

#endif //SPLATOON_SERVER_ARGPARSER_HPP
