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
    async::Task<void> registerEx(ClientInfo client, Request req, std::unique_ptr<List<StationURL>> urls,
                    std::unique_ptr<AnyDataHolder> data);
    async::Task<void> updateAndGetAllInformation(ClientInfo client, Request req, std::unique_ptr<NNAInfo> nnaInfo,
                                    std::unique_ptr<NintendoPresenceV2> presence, std::unique_ptr<Datetime> birthdate);
    async::Task<void> updatePresence(ClientInfo client, Request req, std::unique_ptr<NintendoPresenceV2> presence);

    async::Task<void> onDisconnect(prudp::PRUDPAddress address) override;

    void sendNotification(ClientInfo client, NintendoNotificationType type, uint32_t sender, const AnyDataHolder& data);

    std::shared_ptr<db::Database> db;
    std::string base64JWTKey;

    uint32_t nextRVConnId = 0;
    std::mutex rvConnIdMutex;
    uint32_t nextCallId = 0;
    std::mutex callIdMutex;
    std::unordered_map<uint32_t, FriendsRegisteredClientInfo> registeredClients;
    std::recursive_mutex registeredClientsMutex;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDSSECURE_HPP
