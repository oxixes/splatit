#ifndef SPLATOON_SERVER_MIGRATIONS_HPP
#define SPLATOON_SERVER_MIGRATIONS_HPP

#include "../../logger.hpp"
#include "../database.hpp"

namespace db::migrations {

bool migration_initial_accounts(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);
bool migration_initial_friendsAuth(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);
bool migration_initial_splatoonAuth(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);
bool migration_initial_friends(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);
bool migration_initial_boss(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);
bool migration_initial_management(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);
bool migration_initial_splatoonSecure(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type);

bool migrate(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type, SystemType systemType,
             DBVersion fromVersion);
std::string getVersionString(DBVersion version);
DBVersion getVersionFromString(const std::string& str);

bool runVoidCommandsSync(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db,
                         const std::vector<std::string>& commands, const std::string& rollbackCommand);

} // namespace db

#endif //SPLATOON_SERVER_MIGRATIONS_HPP
