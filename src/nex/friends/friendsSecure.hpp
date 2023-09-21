#ifndef SPLATOON_SERVER_FRIENDSSECURE_HPP
#define SPLATOON_SERVER_FRIENDSSECURE_HPP

#include "../rmc/server.hpp"
#include "../../db/database.hpp"
#include "../types/common/stationURL.hpp"
#include "../types/common/list.hpp"
#include "../types/common/anyDataHolder.hpp"
#include "../types/friendsSecure/NNAInfo.hpp"
#include "../types/friendsSecure/nintendoPresenceV2.hpp"
#include "../types/common/datetime.hpp"

namespace nex::rmc {

class FriendsSecureRMC : public Server {
public:
    explicit FriendsSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                              std::string base64JWTKey);
    ~FriendsSecureRMC() override = default;

private:
    void registerEx(ClientInfo client, Request req, List<StationURL> urls, AnyDataHolder data);
    void updateAndGetAllInformation(ClientInfo client, Request req, NNAInfo nnaInfo);

    bool checkJWT(const std::string& jwtToken, ClientInfo& client);

    std::shared_ptr<db::Database> db;
    std::string base64JWTKey;

    uint32_t nextRVConnId = 0;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDSSECURE_HPP
