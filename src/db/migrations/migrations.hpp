#ifndef SPLATOON_SERVER_MIGRATIONS_HPP
#define SPLATOON_SERVER_MIGRATIONS_HPP

#include "../../logger.hpp"
#include "../database.hpp"

namespace db::migrations {

bool migration_initial(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);

bool migrate(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type,
             DBVersion fromVersion);
std::string getVersionString(DBVersion version);
DBVersion getVersionFromString(const std::string& str);

} // namespace db

#endif //SPLATOON_SERVER_MIGRATIONS_HPP
