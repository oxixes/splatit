#include "argParser.hpp"

#include <stdexcept>
#include <string>

namespace argParser {

namespace {

// Reads the value of an option that takes one, either attached to the same
// argument (--data=path, -dpath) or as the argument after it (--data path).
// Advances the index when it consumes the following argument.
bool takeValue(const int argc, char** argv, int& index, const std::string& attached,
               const bool hasAttached, std::string& out) {
    if (hasAttached) {
        out = attached;
        return !out.empty();
    }

    if (index + 1 >= argc) return false;
    out = argv[++index];
    return true;
}

void applyLogLevel(const std::string& value, options& serverOptions,
                   const std::shared_ptr<Logger::Logger>& logger) {
    try {
        size_t consumed = 0;
        const int level = std::stoi(value, &consumed);
        if (consumed != value.size() || level < 0 || level > 3) throw std::out_of_range("");
        serverOptions.minLogLevel = static_cast<Logger::level>(level);
    } catch (...) {
        logger->log(Logger::level::WARN, Logger::group::SETUP,
                    "Invalid logging level specified, using 1.");
    }
}

} // namespace

bool parseArgs(const int argc, char** argv, options& serverOptions, const std::shared_ptr<Logger::Logger>& logger) {
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];

        // Conventional end of options marker.
        if (arg == "--") break;

        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return false;
        }

        std::string name;
        std::string attached;
        bool hasAttached = false;

        if (arg.rfind("--", 0) == 0) {
            const size_t equals = arg.find('=');
            if (equals == std::string::npos) {
                name = arg;
            } else {
                name = arg.substr(0, equals);
                attached = arg.substr(equals + 1);
                hasAttached = true;
            }
        } else if (arg.size() >= 2 && arg[0] == '-') {
            name = arg.substr(0, 2);
            if (arg.size() > 2) {
                attached = arg.substr(2);
                hasAttached = true;
            }
        } else {
            printHelp(argv[0]);
            return false;
        }

        std::string value;
        if (name == "-d" || name == "--data") {
            if (!takeValue(argc, argv, i, attached, hasAttached, value)) {
                printHelp(argv[0]);
                return false;
            }
            serverOptions.data_path = value;
        } else if (name == "-l" || name == "--log-level") {
            if (!takeValue(argc, argv, i, attached, hasAttached, value)) {
                printHelp(argv[0]);
                return false;
            }
            applyLogLevel(value, serverOptions, logger);
        } else {
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
