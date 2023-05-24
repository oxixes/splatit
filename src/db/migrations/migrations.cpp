#include "migrations.hpp"

namespace db::migrations {

// We store all migrations in an array, so we can iterate over them
std::array<bool (*)(Logger::Logger*, Database*, type), 1> migrations = {
        migration_initial
};

bool migrate(Logger::Logger* logger, Database* db, type type, dbVersion fromVersion) {
    logger->log(Logger::level::INFO, Logger::group::SETUP,
                "Migrating database from version " + getVersionString(fromVersion) +
                " to version " + getVersionString(CURRENT_VERSION));

    for (int i = static_cast<int>(fromVersion) + 1; i <= static_cast<int>(CURRENT_VERSION); i++) {
        if (!migrations[i - 1](logger, db, type)) {
            logger->log(Logger::level::ERROR, Logger::group::SETUP,
                        "Failed to migrate from version " + getVersionString(static_cast<dbVersion>(i - 1)) +
                        " to version " + getVersionString(static_cast<dbVersion>(i)));
            return false;
        }
    }

    return true;
}

std::string getVersionString(dbVersion version) {
    switch (version) {
        case dbVersion::NO_DATA:
            return "new";
        case dbVersion::INITIAL:
            return "0.0.1";
        default:
            return "";
    }
}

} // namespace migrations