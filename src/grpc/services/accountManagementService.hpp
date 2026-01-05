#ifndef SPLATOON_SERVER_ACCOUNTMANAGEMENTSERVICE_HPP
#define SPLATOON_SERVER_ACCOUNTMANAGEMENTSERVICE_HPP

#include <accountManagement.grpc.pb.h>

#include <utility>

#include "../../logger.hpp"
#include "../../db/database.hpp"
#include "../../http/server.hpp"

namespace grpcimpl::accountmanagement::v1 {

class AccountManagementServiceImpl final : public AccountManagementService::CallbackService {
public:
    explicit AccountManagementServiceImpl(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> accountDb,
                                          std::shared_ptr<http::Server> httpServer) :
                                          logger(std::move(logger)), db(std::move(accountDb)),
                                          httpServer(std::move(httpServer)) {}

    grpc::ServerUnaryReactor* GetSecurityStatus(grpc::CallbackServerContext* context,
        const google::protobuf::Empty* _, SecurityStatus* reply) override;

    grpc::ServerUnaryReactor* UpdateSecurityStatus(grpc::CallbackServerContext* context,
        const SecurityStatus* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* GetStoredAgreements(grpc::CallbackServerContext* context,
        const google::protobuf::Empty* _, GetStoredAgreementsResponse* reply) override;

    grpc::ServerUnaryReactor* PublishAgreement(grpc::CallbackServerContext* context,
        const AgreementCreate* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* DeleteAgreement(grpc::CallbackServerContext* context,
        const AgreementDelete* request, google::protobuf::Empty* _) override;

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<db::Database> db;
    std::shared_ptr<http::Server> httpServer;
};

} // namespace grpcimpl::accountmanagement::v1

#endif //SPLATOON_SERVER_ACCOUNTMANAGEMENTSERVICE_HPP
