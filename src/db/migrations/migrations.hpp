#ifndef SPLATOON_SERVER_MIGRATIONS_HPP
#define SPLATOON_SERVER_MIGRATIONS_HPP

#include "../../logger.hpp"
#include "../database.hpp"

namespace migrations {

    enum class dbVersion {
        NO_DATA = 0,
        INITIAL
    };

    const dbVersion CURRENT_VERSION = dbVersion::INITIAL;


    bool migration_initial(Logger::Logger* logger, Database* db, dbType type);

    bool migrate(Logger::Logger* logger, Database* db, dbType type, dbVersion fromVersion);
    std::string getVersionString(dbVersion version);

} // namespace migrations

#endif //SPLATOON_SERVER_MIGRATIONS_HPP
