#include "argParser.hpp"

namespace argParser {

    bool parseArgs(int argc, char** argv, options& serverOptions, const std::shared_ptr<Logger::Logger>& logger) {
        const option args[] = {
                {"no-account",         no_argument, nullptr, 1},
                {"no-boss",            no_argument, nullptr, 2},
                {"no-friends",         no_argument, nullptr, 3},
                {"no-friends-auth",    no_argument, nullptr, 4},
                {"no-friends-secure",  no_argument, nullptr, 5},
                {"no-splatoon",        no_argument, nullptr, 6},
                {"no-splatoon-auth",   no_argument, nullptr, 7},
                {"no-splatoon-secure", no_argument, nullptr, 8},
                {"data", required_argument, nullptr, 'd'},
                {"log-level", required_argument, nullptr, 'l'},
                {"help",               no_argument, nullptr, 'h'}
        };

        while (true) {
            const int argument = getopt_long(argc, argv, "d:l:h", args, nullptr);

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
                case 'd':
                    serverOptions.data_path = std::string(optarg);
                    break;
                case 'l':
                    try {
                        int level = std::stoi(optarg);
                        if (level > 3 || level < 0) throw std::out_of_range("");
                        serverOptions.minLogLevel = static_cast<Logger::level>(level);
                    } catch (...) {
                        logger->log(Logger::level::WARN, Logger::group::SETUP,
                                    "Invalid logging level specified, using 1.");
                    }
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
                                           "-d, --data           Specify the data directory path (Default: ./data).\n"
                                           "-l, --log-level      Specify the minimum log level (Default: 1)\n"
                                           "--no-account         Disables the account server.\n"
                                           "--no-boss            Disables the BOSS server.\n"
                                           "--no-friends         Disables the friends server (auth and secure).\n"
                                           "--no-friends-auth    Disables the friends auth server.\n"
                                           "--no-friends-secure  Disables the friends secure server.\n"
                                           "--no-splatoon        Disables the Splatoon server (auth and secure).\n"
                                           "--no-splatoon-auth   Disables the Splatoon auth server.\n"
                                           "--no-splatoon-secure Disables the Splatoon secure server.\n"
                                           "-h, --help           Show this help message.\n";
    }

} // namespace argParser