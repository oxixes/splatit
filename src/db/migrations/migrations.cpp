#include "migrations.hpp"

namespace db::migrations {

// We store all migrations in an array, so we can iterate over them
std::vector<bool (*)(const std::shared_ptr<Logger::Logger>&, const std::shared_ptr<Database>&, DBType)> accountMigrations = {
        migration_initial_accounts
};

std::vector<bool (*)(const std::shared_ptr<Logger::Logger>&, const std::shared_ptr<Database>&, DBType)> friendsAuthMigrations = {
        migration_initial_friendsAuth
};

std::vector<bool (*)(const std::shared_ptr<Logger::Logger>&, const std::shared_ptr<Database>&, DBType)> splatoonAuthMigrations = {
        migration_initial_splatoonAuth
};

std::vector<bool (*)(const std::shared_ptr<Logger::Logger>&, const std::shared_ptr<Database>&, DBType)> friendsMigrations = {
        migration_initial_friends
};

bool migrate(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type, SystemType systemType,
             DBVersion fromVersion) {
    logger->log(Logger::level::INFO, Logger::group::DB,
                "Migrating database from version " + getVersionString(fromVersion) +
                " to version " + getVersionString(CURRENT_VERSION));

    for (int i = static_cast<int>(fromVersion) + 1; i <= static_cast<int>(CURRENT_VERSION); i++) {
        std::vector<bool (*)(const std::shared_ptr<Logger::Logger>&, const std::shared_ptr<Database>&, DBType)>* migrations;
        switch (systemType) {
            case SystemType::ACCOUNTS:
                migrations = &accountMigrations;
                break;
            case SystemType::FRIENDS_AUTH:
                migrations = &friendsAuthMigrations;
                break;
            case SystemType::SPLATOON_AUTH:
                migrations = &splatoonAuthMigrations;
                break;
            case SystemType::FRIENDS_SECURE:
                migrations = &friendsMigrations;
                break;
            default:
                logger->log(Logger::level::FAILURE, Logger::group::DB,
                            "Unknown system type for migration: " + std::to_string(static_cast<int>(systemType)));
                return false;
        }

        if (!(*migrations)[i - 1](logger, db, type)) {
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

bool runVoidCommandsSync(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db,
                         const std::vector<std::string>& commands, const std::string& rollbackCommand) {
    std::shared_ptr<std::mutex> queueMutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::queue<std::shared_ptr<Promise>>> promisesQueue = std::make_shared<std::queue<std::shared_ptr<Promise>>>();

    for (auto & sqlCmd : commands) {
        std::unique_ptr<Result> result = nullptr;

        std::unique_ptr<Command> command = Database::craftVoidCommand(sqlCmd);
        db->queueCommand(std::move(command), queueMutex, nullptr, promisesQueue)->then([&result](std::any&& resAny) {
            result = std::move(std::make_unique<Result>(std::move(std::any_cast<Result>(resAny))));
        });
        db->processQueue();
        db->waitForQueue();

        std::unique_lock lock(*queueMutex);
        if (!promisesQueue->empty()) {
            promisesQueue->front()->resolve();
            promisesQueue->pop();
        }
        lock.unlock();

        if (result == nullptr) {
            logger->log(Logger::level::FAILURE, Logger::group::DB, "Failed to get result for command: " + sqlCmd);
            return false;
        }

        if (result->getStatus() != DBResultStatus::SUCCESS) {
            // Rollback transaction
            command = Database::craftVoidCommand(rollbackCommand);
            db->queueCommand(std::move(command), queueMutex, nullptr, promisesQueue)->then([&result](std::any&& resAny){
                result = std::move(std::make_unique<Result>(std::move(std::any_cast<Result>(resAny))));
            });
            db->processQueue();
            db->waitForQueue();

            logger->log(Logger::level::FAILURE, Logger::group::DB, "Failed to execute command: " + sqlCmd);
            return false;
        }
    }

    return true;
}

} // namespace migrations