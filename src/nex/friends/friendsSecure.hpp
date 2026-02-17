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
#include "../types/friendsSecure/blacklistedPrincipal.hpp"
#include "../types/friendsSecure/nintendoNotificationEvent.hpp"
#include "../types/friendsSecure/principalPreference.hpp"
#include "../types/friendsSecure/comment.hpp"
#include "../types/friendsSecure/friendRequestMsg.hpp"
#include "../types/friendsSecure/persistentNotification.hpp"
#include "../../sharedState/sharedState.hpp"
#include "../../grpc/channelPool.hpp"

#define FRIENDS_SETTING_STATUS 0

namespace nex::rmc {

struct UserPreference {
    bool showOnline = true;
    bool showPlaying = true;
    bool blockFriendRequest = false;
};

struct UserData {
    uint32_t pid{};
    UserPreference preference;
};

struct FriendsRegisteredClientInfo {
    ClientInfo client;
    UserData userData;
    std::set<uint32_t> friends;
};

class FriendsSecureRMC : public Server {
public:
    explicit FriendsSecureRMC(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> db,
                              std::string base64JWTKey, std::shared_ptr<ss::SharedState> sharedState, uint32_t serverId,
                              int gRCPPoolMaxSize, int gRCPRequestTimeout);
    ~FriendsSecureRMC() override = default;

    async::Task<bool> deleteAccount(uint32_t pid) const;

    async::Task<bool> sendNotification(const ClientInfo &client, NintendoNotificationType type, uint32_t sender,
        const AnyDataHolder& data, bool dontResend = false);
    async::Task<uint32_t> getConnectedClientCount() const;
private:
    async::Task<void> register_(ClientInfo client, Request req,
                                std::unique_ptr<List<StationURL>> urls);
    async::Task<void> registerEx(ClientInfo client, Request req, std::unique_ptr<List<StationURL>> urls,
                    std::unique_ptr<AnyDataHolder> data);
    async::Task<void> nintendoCreateAccount(ClientInfo client, Request req, std::unique_ptr<String> principalName,
                    std::unique_ptr<String> key, std::unique_ptr<UInt32> groups, std::unique_ptr<String> email,
                    std::unique_ptr<AnyDataHolder> data);
    async::Task<void> updateAndGetAllInformation(ClientInfo client, Request req, std::unique_ptr<NNAInfo> nnaInfo,
                                    std::unique_ptr<NintendoPresenceV2> presence, std::unique_ptr<Datetime> birthdate);
    async::Task<void> addFriend(ClientInfo client, Request req, std::unique_ptr<PID> pid);
    async::Task<void> addFriendByName(ClientInfo client, Request req, std::unique_ptr<String> username);
    async::Task<void> removeFriend(ClientInfo client, Request req, std::unique_ptr<PID> pid);
    async::Task<void> addFriendRequest(ClientInfo client, Request req, std::unique_ptr<PID> pid, std::unique_ptr<UInt8> unk1,
                                       std::unique_ptr<String> message, std::unique_ptr<UInt8> unk2, std::unique_ptr<String> unk3,
                                       std::unique_ptr<GameKey> gameKey, std::unique_ptr<Datetime> unk4);
    async::Task<void> cancelFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id);
    async::Task<void> acceptFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id);
    async::Task<void> deleteFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id);
    async::Task<void> denyFriendRequest(ClientInfo client, Request req, std::unique_ptr<UInt64> id);
    async::Task<void> markFriendRequestsAsReceived(ClientInfo client, Request req, std::unique_ptr<List<UInt64>> requests);
    async::Task<void> addBlackList(ClientInfo client, Request req, std::unique_ptr<BlacklistedPrincipal> blacklist);
    async::Task<void> removeBlackList(ClientInfo client, Request req, std::unique_ptr<PID> pid);
    async::Task<void> updatePresence(ClientInfo client, Request req, std::unique_ptr<NintendoPresenceV2> presence);
    async::Task<void> updateMii(ClientInfo client, Request req, std::unique_ptr<MiiV2> mii);
    async::Task<void> updateComment(ClientInfo client, Request req, std::unique_ptr<Comment> comment);
    async::Task<void> updatePreference(ClientInfo client, Request req, std::unique_ptr<PrincipalPreference> preference);
    async::Task<void> getBasicInfo(ClientInfo client, Request req, std::unique_ptr<List<PID>> pids);
    async::Task<void> deletePersistentNotification(ClientInfo client, Request req, std::unique_ptr<List<PersistentNotification>> notifications);
    async::Task<void> checkSettingStatus(ClientInfo client, Request req);
    async::Task<void> getRequestBlockSettings(ClientInfo client, Request req, std::unique_ptr<List<PID>> pids);

    async::Task<void> onDisconnect(prudp::PRUDPAddress address) override;

    async::Task<bool> cleanupExpiredFriendRequests(uint32_t pid) const;
    async::Task<void> createBecameFriendsPersistentNotification(uint32_t pid, uint32_t friendPid) const;

    async::Task<void> addFriendInternal(ClientInfo client, Request req, std::unique_ptr<PID> pid, std::unique_ptr<FriendRequestMsg> message);

    std::shared_ptr<db::Database> db;
    std::string base64JWTKey;
    std::shared_ptr<ss::SharedState> sharedState;

    uint32_t nextRVConnId = 0;
    std::mutex rvConnIdMutex;
    uint32_t nextCallId = 0;
    std::mutex callIdMutex;

    std::shared_ptr<grpcimpl::ChannelPool> channelPool;
    int gRCPRequestTimeout;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_FRIENDSSECURE_HPP
