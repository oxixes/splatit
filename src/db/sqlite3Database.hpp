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

namespace db {

class sqlite3Database final : public Database {
public:
    sqlite3Database(std::shared_ptr<Logger::Logger> logger, const fs::path& dbPath);
    ~sqlite3Database() override;

    bool init() override;
    bool run() override;
    void close() override;

    int queueCommand(Command*, bool commandMutex) override;
    void processQueue() override;
    void waitForCommand(int commandId, bool* shouldEnd) override;
    void waitForQueue(bool* shouldEnd) override;
    void clearCommandMutex(int commandId) override;

private:
    sqlite3* db = nullptr;
    fs::path dbPath;

    bool running = false;
    std::thread dbThreadHandle;
    std::mutex dbThreadMutex;
    std::condition_variable dbThreadCV;

    void dbThread();

    void processCommand(Command* command);
    bool craftStatement(const std::string& command, sqlite3_stmt** outStatement);
    bool bindData(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                  const std::vector<DBData*>& data);
    bool runStatement(sqlite3_stmt* statement, const std::vector<dbDataType>& dataTypes,
                      std::vector<std::vector<DBData*>*>* returnedData);

    static void freeData(std::vector<std::vector<DBData*>*>* data);
};

} // namespace db

#endif //SPLATOON_SERVER_SQLITE3DATABASE_HPP
