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
#include "../types/friendsSecure/friendInfo.hpp"
#include "../types/friendsSecure/nintendoNotificationEvent.hpp"

namespace nex::rmc {

struct FriendsRegisteredClientInfo {
    ClientInfo client;
    std::vector<uint32_t> friends;
};

class FriendsSecureRMC : public Server {
public:
    explicit FriendsSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                              std::string base64JWTKey);
    ~FriendsSecureRMC() override = default;

private:
    void registerEx(ClientInfo client, Request req, List<StationURL> urls, AnyDataHolder data);
    void updateAndGetAllInformation(ClientInfo client, Request req, NNAInfo nnaInfo, NintendoPresenceV2 presence,
                                    Datetime birthdate);
    void updatePresence(ClientInfo client, Request req, NintendoPresenceV2 presence);

    void onDisconnect(prudp::PRUDPAddress address) override;

    void sendNotification(ClientInfo client, NintendoNotificationType type, uint32_t sender, const AnyDataHolder& data);

    std::shared_ptr<db::Database> db;
    std::string base64JWTKey;

    uint32_t nextRVConnId = 0;
    uint32_t nextCallId = 0;
    std::unordered_map<uint32_t, FriendsRegisteredClientInfo> registeredClients;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDSSECURE_HPP
