#ifndef SPLATOON_SERVER_SQLITE3DATABASE_HPP
#define SPLATOON_SERVER_SQLITE3DATABASE_HPP

#include <sqlite3.h>

#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "database.hpp"
#include "dbTypes.hpp"
#include "../util/util.hpp"

namespace fs = std::filesystem;

class sqlite3Database : public Database {
public:
    sqlite3Database(Logger::Logger* logger, const fs::path& dbPath);
    ~sqlite3Database() override = default;

    bool init() override;
    bool run() override;
    void close() override;

    int queueCommand(DBCommand* command) override;
    void processQueue() override;
    void waitForCommand(int commandId) override;
    void waitForQueue() override;

private:
    sqlite3* db = nullptr;
    fs::path dbPath;

    bool running = false;
    std::thread dbThreadHandle;
    std::mutex dbThreadMutex;
    std::condition_variable dbThreadCV;

public:
    void dbThread();

    bool craftCommand(const std::string& command, sqlite3_stmt** outStatement);
    // TODO Bind command parameters
    bool runStatement(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                      std::vector<std::vector<DBData*>*>* returnedData);

    static void freeData(std::vector<std::vector<DBData*>*>* data);
};

#endif //SPLATOON_SERVER_SQLITE3DATABASE_HPP
