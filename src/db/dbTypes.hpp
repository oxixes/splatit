#ifndef SPLATOON_SERVER_DBTYPES_HPP
#define SPLATOON_SERVER_DBTYPES_HPP

#include <string>
#include <utility>
#include <any>
#include <vector>

namespace db {

enum class DBDataType {
    INTEGER,
    STRING,
    BLOB,
    DATETIME,
    NULL_T
};

class DBData {
public:
    std::any data;
    DBDataType type;
    int id = -1;

    virtual ~DBData() = default;

protected:
    DBData(std::any data, DBDataType type) {
        this->data = std::move(data);
        this->type = type;
    }
};

class DBInteger : public DBData {
public:
    explicit DBInteger(int64_t data) : DBData(data, DBDataType::INTEGER) {}
};

class DBString : public DBData {
public:
    explicit DBString(const std::string& data) : DBData(data, DBDataType::STRING) {}
};

class DBBlob : public DBData {
public:
    explicit DBBlob(const std::vector<uint8_t>& data) : DBData(data, DBDataType::BLOB) {}
};

typedef std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> datetime_t;

class DBDateTime : public DBData {
public:
    explicit DBDateTime(datetime_t data) : DBData(data, DBDataType::DATETIME) {}
};

class DBNull : public DBData {
public:
    explicit DBNull() : DBData(nullptr, DBDataType::NULL_T) {}
};


} // namespace db

#endif //SPLATOON_SERVER_DBTYPES_HPP
