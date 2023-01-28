#include "argParser.hpp"

namespace argParser {

    bool parseArgs(int argc, char** argv, options& serverOptions) {
        const option args[] = {
                {"no-account",         no_argument, nullptr, 1},
                {"no-boss",            no_argument, nullptr, 2},
                {"no-friends",         no_argument, nullptr, 3},
                {"no-friends-auth",    no_argument, nullptr, 4},
                {"no-friends-secure",  no_argument, nullptr, 5},
                {"no-splatoon",        no_argument, nullptr, 6},
                {"no-splatoon-auth",   no_argument, nullptr, 7},
                {"no-splatoon-secure", no_argument, nullptr, 8},
                {"help",               no_argument, nullptr, 'h'}
        };

        while (true) {
            const int argument = getopt_long(argc, argv, "h", args, nullptr);

            if (argument == -1) break;
            switch (argument) {
                case 1:
                    serverOptions.no_account = true;
                    break;
                case 2:
                    serverOptions.no_boss = true;
                    break;
                case 3:
                    serverOptions.no_friends_auth = true;
                    serverOptions.no_friends_secure = true;
                    break;
                case 4:
                    serverOptions.no_friends_auth = true;
                    break;
                case 5:
                    serverOptions.no_friends_secure = true;
                    break;
                case 6:
                    serverOptions.no_splatoon_auth = true;
                    serverOptions.no_splatoon_secure = true;
                    break;
                case 7:
                    serverOptions.no_splatoon_auth = true;
                    break;
                case 8:
                    serverOptions.no_splatoon_secure = true;
                    break;
                case 'h':
                case '?':
                default:
                    printHelp(argv[0]);
                    return false;
            }
        }

        return true;
    }

    void printHelp(const char* argv0) {
        std::cout << "Usage: " << argv0 << " [options]\n"
                                           "--no-account         Disables the account server.\n"
                                           "--no-boss            Disables the BOSS server.\n"
                                           "--no-friends         Disables the friends server (auth and secure).\n"
                                           "--no-friends-auth    Disables the friends auth server.\n"
                                           "--no-friends-secure  Disables the friends secure server.\n"
                                           "--no-splatoon        Disables the Splatoon server (auth and secure).\n"
                                           "--no-splatoon-auth   Disables the Splatoon auth server.\n"
                                           "--no-splatoon-secure Disables the Splatoon secure server.\n";
    }

} // namespace argParser