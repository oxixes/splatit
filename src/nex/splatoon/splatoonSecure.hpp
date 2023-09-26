#ifndef SPLATOON_SERVER_SPLATOONSECURE_HPP
#define SPLATOON_SERVER_SPLATOONSECURE_HPP

#include <memory>
#include "../rmc/server.hpp"
#include "../../db/database.hpp"

namespace nex::rmc {

    class SplatoonSecureRMC : public Server {
    public:
        explicit SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db);
        ~SplatoonSecureRMC() override = default;

    private:
        std::shared_ptr<db::Database> db;

        uint32_t nextRVConnId = 0;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_SPLATOONSECURE_HPP
