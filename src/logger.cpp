#include "logger.hpp"

namespace Logger {

    void Logger::log(level level, group group, const std::string& msg) {
        if (static_cast<int>(level) < static_cast<int>(minLoggingLevel)) return;

        std::unique_lock lock(logMutex);

        time_t currentTime = std::time(nullptr);
        tm localTime = *std::localtime(&currentTime);

        std::cout << "[" << std::put_time(&localTime, "%d/%m/%Y %H:%M:%S") << "] "
                << "[" << getLevelName(level) << "] "
                << "[" << getGroupName(group) << "] "
                << msg << "\n";
    }

    void Logger::setMinLevel(level minLevel) {
        minLoggingLevel = minLevel;
    }

    std::string Logger::getLevelName(level level) {
        switch (level) {
            case level::ERROR:
                return "ERROR";
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
            case group::SETUP:
                return "SETUP";
            default:
                return "UNKNOWN";
        }
    }

} // namespace Logger