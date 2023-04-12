#ifndef SPLATOON_SERVER_DATABASE_HPP
#define SPLATOON_SERVER_DATABASE_HPP

class Database {
protected:
    Database() = default;

public:
    virtual ~Database() = default;

    virtual bool init() = 0;
    virtual void close() = 0;
};

#endif //SPLATOON_SERVER_DATABASE_HPP
