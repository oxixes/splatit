#ifndef SPLATOON_SERVER_FRIENDSSERVICE_HPP
#define SPLATOON_SERVER_FRIENDSSERVICE_HPP

#include <friends.grpc.pb.h>

#include "../../logger.hpp"
#include "../../nex/friends/friendsSecure.hpp"

namespace grpcimpl::friends::v1 {

class FriendsServiceImpl final : public FriendsService::CallbackService {
public:
    explicit FriendsServiceImpl(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsRMC) :
                             logger(std::move(logger)),
                             friendsRMC(std::move(friendsRMC)) {}

    grpc::ServerUnaryReactor* SendNotification(grpc::CallbackServerContext* context,
        const SendNotificationRequest* request,
        google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* GetConnectedClientCount(grpc::CallbackServerContext* context,
        const google::protobuf::Empty* _,
        GetConnectedClientCountResponse* response) override;

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsRMC;
};

} // namespace grpcimpl::friends::v1

#endif //SPLATOON_SERVER_FRIENDSSERVICE_HPP