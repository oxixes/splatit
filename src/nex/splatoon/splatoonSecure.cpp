#include "splatoonSecure.hpp"

namespace nex::rmc {

SplatoonSecureRMC::SplatoonSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db) :
                                        Server(std::move(logger)), db(std::move(db)) {
    logGroup = Logger::group::SPLATOON_SECURE;
}

} // namespace nex::rmc