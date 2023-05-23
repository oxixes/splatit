#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

#include "../logger.hpp"

enum class dbType {
    SQLITE3
};

class Database {
protected:
    explicit Database(Logger::Logger* logger);

    Logger::Logger* logger;

public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual void close() = 0;
};

#endif //SPLATOON_SERVER_DATABASE_HPP
