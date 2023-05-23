#ifndef SPLATOON_SERVER_SQLITE3DATABASE_HPP
#define SPLATOON_SERVER_SQLITE3DATABASE_HPP

#include <sqlite3.h>

#include <filesystem>

#include "database.hpp"
#include "dbTypes.hpp"
#include "../util/util.hpp"

namespace fs = std::filesystem;

class sqlite3Database : public Database {
public:
    sqlite3Database(Logger::Logger* logger, const fs::path& dbPath);
    ~sqlite3Database() override = default;

    bool init() override;
    void close() override;

    bool craftCommand(const std::string& command, sqlite3_stmt** outStatement);
    // TODO Bind command parameters
private:
    sqlite3* db = nullptr;
    fs::path dbPath;

public:
    bool runStatement(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                      std::vector<std::vector<DBData*>*>* returnedData);

    static void freeData(std::vector<std::vector<DBData*>*>* data);
};

#endif //SPLATOON_SERVER_SQLITE3DATABASE_HPP
