#ifndef SPLATOON_SERVER_DBTYPES_HPP
#define SPLATOON_SERVER_DBTYPES_HPP

#include <string>
#include <utility>
#include <any>

namespace db {

enum class dbDataType {
    INTEGER,
    STRING
};

class DBData {
public:
    std::any data;
    dbDataType type;
    int id = -1;

    virtual ~DBData() = default;

protected:
    DBData(std::any data, dbDataType type) {
        this->data = std::move(data);
        this->type = type;
    }
};

class DBInteger : public DBData {
public:
    explicit DBInteger(int data) : DBData(data, dbDataType::INTEGER) {}
};

class DBString : public DBData {
public:
    explicit DBString(const std::string& data) : DBData(data, dbDataType::STRING) {}
};

} // namespace db

#endif //SPLATOON_SERVER_DBTYPES_HPP
