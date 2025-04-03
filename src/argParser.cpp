#include "argParser.hpp"

namespace argParser {

    bool parseArgs(const int argc, char** argv, options& serverOptions, const std::shared_ptr<Logger::Logger>& logger) {
        constexpr option args[] = {
                {"data", required_argument, nullptr, 'd'},
                {"log-level", required_argument, nullptr, 'l'},
                {"help",               no_argument, nullptr, 'h'}
        };

        while (true) {
            const int argument = getopt_long(argc, argv, "d:l:h", args, nullptr);

            if (argument == -1) break;
            switch (argument) {
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
                                           "-h, --help           Show this help message.\n";
    }

} // namespace argParser