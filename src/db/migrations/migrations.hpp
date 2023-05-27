#ifndef SPLATOON_SERVER_MIGRATIONS_HPP
#define SPLATOON_SERVER_MIGRATIONS_HPP

#include "../../logger.hpp"
#include "../database.hpp"

namespace db::migrations {

enum class dbVersion {
    NO_DATA = 0,
    INITIAL
};

const dbVersion CURRENT_VERSION = dbVersion::INITIAL;


bool migration_initial(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, type type);

bool migrate(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, type type,
             dbVersion fromVersion);
std::string getVersionString(dbVersion version);

} // namespace db

#endif //SPLATOON_SERVER_MIGRATIONS_HPP
