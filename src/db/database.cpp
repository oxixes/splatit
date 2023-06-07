#include "database.hpp"
#include "sqlite3Database.hpp"

#include <utility>

namespace db {

Database::Database(std::shared_ptr<Logger::Logger> logger, DBType type, DBVersion version) {
    this->logger = std::move(logger);
    this->dbType = type;
    this->dbVersion = version;
}

std::unique_ptr<Command> Database::craftVoidCommand(const std::string& command) {
    auto dbCommand = std::make_unique<Command>(db::DBCommandType::GENERIC,
        std::vector<std::any>{std::any(command), std::any(std::vector<dbDataType>{}),
        std::any(std::vector<std::shared_ptr<DBData>>{}), std::any(std::vector<dbDataType>{})});

    return std::move(dbCommand);
}

std::unique_ptr<Result> Database::getResult(int commandID) {
    std::unique_lock<std::mutex> lock(resultsMutex);

    auto result = std::find_if(this->results.begin(), this->results.end(),
        [commandID](const std::unique_ptr<Result>& result) {
            return result->commandId == commandID;
        });

    if (result == this->results.end()) return nullptr;

    auto resultPtr = std::move(*result);
    results.erase(result);

    return std::move(resultPtr);
}

std::shared_ptr<Database> Database::createDatabase(const json& config, std::shared_ptr<Logger::Logger> logger) {
    if (config["type"].get<std::string>() == "SQLite3") {
        auto* db = new sqlite3Database(std::move(logger), config["path"].get<std::string>());
        return std::move(std::shared_ptr<Database>((Database*) db));
    } else {
        logger->log(Logger::level::ERROR, Logger::group::DB, "Database type " +
            config["type"].get<std::string>() + " is not supported.");
        return nullptr;
    }
}

DBType Database::getType() const {
    return this->dbType;
}

DBVersion Database::getVersion() const {
    return this->dbVersion;
}

} // namespace db