#ifndef SPLATOON_SERVER_ACCOUNTMANAGEMENTSERVICE_HPP
#define SPLATOON_SERVER_ACCOUNTMANAGEMENTSERVICE_HPP

#include <accountManagement.grpc.pb.h>

#include <utility>

#include "../../logger.hpp"
#include "../../db/database.hpp"
#include "../../http/server.hpp"
#include "../../settingsManager.hpp"

namespace grpcimpl::accountmanagement::v1 {

class AccountManagementServiceImpl final : public AccountManagementService::CallbackService {
public:
    explicit AccountManagementServiceImpl(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<db::Database> accountDb,
                                          std::shared_ptr<http::Server> httpServer,
                                          std::shared_ptr<SettingsManager> settingsManager) :
                                          logger(std::move(logger)), db(std::move(accountDb)),
                                          httpServer(std::move(httpServer)), settingsManager(std::move(settingsManager)) {}

    grpc::ServerUnaryReactor* GetSecurityStatus(grpc::CallbackServerContext* context,
        const google::protobuf::Empty* _, SecurityStatus* reply) override;

    grpc::ServerUnaryReactor* UpdateSecurityStatus(grpc::CallbackServerContext* context,
        const SecurityStatus* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* GetStoredAgreements(grpc::CallbackServerContext* context,
        const GetStoredAgreementsRequest* request, GetStoredAgreementsResponse* reply) override;

    grpc::ServerUnaryReactor* PublishAgreement(grpc::CallbackServerContext* context,
        const AgreementCreate* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* DeleteAgreement(grpc::CallbackServerContext* context,
        const AgreementDelete* request, google::protobuf::Empty* _) override;

    // Device Management
    grpc::ServerUnaryReactor* CreateDevice(grpc::CallbackServerContext* context,
        const DeviceCreate* request, Device* reply) override;

    grpc::ServerUnaryReactor* GetDevice(grpc::CallbackServerContext* context,
        const DeviceGetRequest* request, Device* reply) override;

    grpc::ServerUnaryReactor* ListDevices(grpc::CallbackServerContext* context,
        const ListDevicesRequest* request, ListDevicesResponse* reply) override;

    grpc::ServerUnaryReactor* UpdateDevice(grpc::CallbackServerContext* context,
        const DeviceUpdate* request, Device* reply) override;

    grpc::ServerUnaryReactor* DeleteDevice(grpc::CallbackServerContext* context,
        const DeviceDeleteRequest* request, google::protobuf::Empty* _) override;

    // Account Management
    grpc::ServerUnaryReactor* CreateAccount(grpc::CallbackServerContext* context,
        const AccountCreate* request, Account* reply) override;

    grpc::ServerUnaryReactor* GetAccount(grpc::CallbackServerContext* context,
        const AccountGetRequest* request, Account* reply) override;

    grpc::ServerUnaryReactor* GetAccountByUsername(grpc::CallbackServerContext* context,
        const AccountGetByUsernameRequest* request, Account* reply) override;

    grpc::ServerUnaryReactor* ListAccounts(grpc::CallbackServerContext* context,
        const ListAccountsRequest* request, ListAccountsResponse* reply) override;

    grpc::ServerUnaryReactor* UpdateAccount(grpc::CallbackServerContext* context,
        const AccountUpdate* request, Account* reply) override;

    grpc::ServerUnaryReactor* DeleteAccount(grpc::CallbackServerContext* context,
        const AccountDeleteRequest* request, google::protobuf::Empty* _) override;

    // Account Email Management
    grpc::ServerUnaryReactor* UpdateAccountEmail(grpc::CallbackServerContext* context,
        const UpdateAccountEmailRequest* request, AccountEmail* reply) override;

    // Account Mii Management
    grpc::ServerUnaryReactor* SetAccountMii(grpc::CallbackServerContext* context,
        const SetAccountMiiRequest* request, AccountMii* reply) override;

    // Account Agreement Management
    grpc::ServerUnaryReactor* AddAccountAgreement(grpc::CallbackServerContext* context,
        const AddAccountAgreementRequest* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* RemoveAccountAgreement(grpc::CallbackServerContext* context,
        const RemoveAccountAgreementRequest* request, google::protobuf::Empty* _) override;

    // Account Device Ownership Management
    grpc::ServerUnaryReactor* LinkDeviceToAccount(grpc::CallbackServerContext* context,
        const LinkDeviceRequest* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* UnlinkDeviceFromAccount(grpc::CallbackServerContext* context,
        const UnlinkDeviceRequest* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* UpdateDeviceStatus(grpc::CallbackServerContext* context,
        const UpdateDeviceStatusRequest* request, google::protobuf::Empty* _) override;

    // Account-Device Attribute Management
    grpc::ServerUnaryReactor* SetAccountDeviceAttribute(grpc::CallbackServerContext* context,
        const SetAccountDeviceAttributeRequest* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* RemoveAccountDeviceAttribute(grpc::CallbackServerContext* context,
        const RemoveAccountDeviceAttributeRequest* request, google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* ListAccountDeviceAttributes(grpc::CallbackServerContext* context,
        const ListAccountDeviceAttributesRequest* request, ListAccountDeviceAttributesResponse* reply) override;

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<db::Database> db;
    std::shared_ptr<http::Server> httpServer;
    std::shared_ptr<SettingsManager> settingsManager;
};

} // namespace grpcimpl::accountmanagement::v1

#endif //SPLATOON_SERVER_ACCOUNTMANAGEMENTSERVICE_HPP
