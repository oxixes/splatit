#ifndef SPLATOON_SERVER_FRIENDSSERVICE_HPP
#define SPLATOON_SERVER_FRIENDSSERVICE_HPP

#include <internalAccountManagement.grpc.pb.h>

#include "../../nex/friends/friendsSecure.hpp"

namespace grpcimpl::internalaccountmanagement::v1 {

class InternalAccountManagementServiceImpl final : public InternalAccountManagementService::CallbackService {
public:
    explicit InternalAccountManagementServiceImpl(std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsSecure,
                                                  std::shared_ptr<Logger::Logger> logger) :
                                                  logger(std::move(logger)),
                                                  friendsSecure(std::move(friendsSecure)) {}

    grpc::ServerUnaryReactor* DeleteServerAccount(grpc::CallbackServerContext* context,
        const DeleteServerAccountRequest* request, google::protobuf::Empty* _) override;

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<nex::rmc::FriendsSecureRMC> friendsSecure;
};

} // namespace grpcimpl::internalaccountmanagement::v1

#endif //SPLATOON_SERVER_FRIENDSSERVICE_HPP