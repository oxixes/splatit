#include "logger.hpp"

namespace Logger {

void Logger::log(level level, group group, const std::string& msg) {
    if (static_cast<int>(level) < static_cast<int>(minLoggingLevel)) return;

    std::unique_lock lock(logMutex);

    time_t currentTime = std::time(nullptr);
    tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &currentTime);
#else
    localtime_r(&currentTime, &localTime);
#endif

    std::cout << "[" << std::put_time(&localTime, "%d/%m/%Y %H:%M:%S") << "] "
            << "[" << getLevelName(level) << "] "
            << "[" << getGroupName(group) << "] "
            << msg << "\n";

    // Always flush, including in release builds. Whenever stdout is a pipe
    // rather than a terminal, which is the case under Docker, systemd and any
    // redirect to a file, the stream is block buffered and nothing appears
    // until several kilobytes have piled up. That turns the log into a useless
    // diagnostic exactly where it matters most. At the default log level the
    // volume is low enough that the extra syscall costs nothing.
    std::cout.flush();
}

void Logger::setMinLevel(level minLevel) {
    minLoggingLevel = minLevel;
}

std::string Logger::getLevelName(level level) {
    switch (level) {
        case level::FAILURE:
            return "FAILURE";
        case level::WARN:
            return "WARNING";
        case level::INFO:
            return "INFO";
        case level::DEBUG:
            return "DEBUG";
        default:
            return "UNKNOWN";
    }
}

std::string Logger::getGroupName(group group) {
    switch (group) {
        case group::ACCOUNT:
            return "ACCOUNT";
        case group::DB:
            return "DB";
        case group::NETWORK:
            return "NETWORK";
        case group::BOSS:
            return "BOSS";
        case group::FRIENDS:
            return "FRIENDS";
        case group::FRIENDS_AUTH:
            return "FRIENDS AUTH";
        case group::FRIENDS_SECURE:
            return "FRIENDS SECURE";
        case group::SPLATOON:
            return "SPLATOON";
        case group::SPLATOON_AUTH:
            return "SPLATOON AUTH";
        case group::SPLATOON_SECURE:
            return "SPLATOON SECURE";
        case group::GRPC:
            return "GRPC";
        case group::SETUP:
            return "SETUP";
        case group::MANAGEMENT:
            return "MANAGEMENT";
        case group::GLOBAL_TASKS:
            return "GLOBAL TASKS";
        case group::REDIS:
            return "REDIS";
        default:
            return "UNKNOWN";
    }
}

} // namespace Logger