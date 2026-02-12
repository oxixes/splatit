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

#ifndef NDEBUG
    // Flush the stream to ensure the message is written immediately
    std::cout.flush();
#endif
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