#include "migrations.hpp"

namespace db::migrations {

// We store all migrations in an array, so we can iterate over them
std::array<bool (*)(const std::shared_ptr<Logger::Logger>&, const std::shared_ptr<Database>&, DBType), 1> migrations = {
        migration_initial
};

bool migrate(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type, DBVersion fromVersion) {
    logger->log(Logger::level::INFO, Logger::group::DB,
                "Migrating database from version " + getVersionString(fromVersion) +
                " to version " + getVersionString(CURRENT_VERSION));

    for (int i = static_cast<int>(fromVersion) + 1; i <= static_cast<int>(CURRENT_VERSION); i++) {
        if (!migrations[i - 1](logger, db, type)) {
            logger->log(Logger::level::FAILURE, Logger::group::DB,
                        "Failed to migrate from version " + getVersionString(static_cast<DBVersion>(i - 1)) +
                        " to version " + getVersionString(static_cast<DBVersion>(i)));
            return false;
        }
    }

    return true;
}

std::string getVersionString(DBVersion version) {
    switch (version) {
        case DBVersion::EMPTY:
            return "new";
        case DBVersion::INITIAL:
            return "0.0.1";
        default:
            return "";
    }
}

DBVersion getVersionFromString(const std::string& str) {
    if (str == "0.0.1") {
        return DBVersion::INITIAL;
    } else {
        throw std::runtime_error("Unknown version string: " + str);
    }
}

} // namespace migrations