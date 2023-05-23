#ifndef SPLATOON_SERVER_LOGGER_HPP
#define SPLATOON_SERVER_LOGGER_HPP

#include <string>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <mutex>

namespace Logger {

    enum class level {
        DEBUG = 0,
        INFO,
        WARN,
        ERROR
    };

    enum class group {
        SETUP,
        DB,
        ACCOUNT,
        BOSS,
        FRIENDS,
        FRIENDS_AUTH,
        FRIENDS_SECURE,
        SPLATOON,
        SPLATOON_AUTH,
        SPLATOON_SECURE
    };

    class Logger {
    public:
        Logger() = default;
        ~Logger() = default;

        void log(level level, group group, const std::string& msg);
        void setMinLevel(level minLevel);

    private:
        level minLoggingLevel = level::INFO;
        std::mutex logMutex; // We use a lock to prevent multiple threads from writing to the log at the same time

        static std::string getLevelName(level level);
        static std::string getGroupName(group group);
    };

} // namespace Logger

#endif //SPLATOON_SERVER_LOGGER_HPP
