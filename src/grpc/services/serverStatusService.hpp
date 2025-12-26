#ifndef SPLATOON_SERVER_SERVERSTATUSSERVICE_HPP
#define SPLATOON_SERVER_SERVERSTATUSSERVICE_HPP

#include <serverStatus.grpc.pb.h>

#include "../../logger.hpp"
#include "../../settingsManager.hpp"

namespace grpcimpl::serverstatus::v1 {

class ServerStatusServiceImpl final : public ServerStatusService::CallbackService {
public:
    explicit ServerStatusServiceImpl(std::shared_ptr<Logger::Logger> logger,
                                     std::shared_ptr<SettingsManager> settingsMgr) :
                                     logger(std::move(logger)),
                                     settingsMgr(std::move(settingsMgr)) {}

    grpc::ServerUnaryReactor* GetServerStatus(grpc::CallbackServerContext* context,
                                              const GetServerStatusRequest* request,
                                              GetServerStatusResponse* reply) override;
private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<SettingsManager> settingsMgr;
};

} // namespace grpcimpl::serverstatus::v1

#endif //SPLATOON_SERVER_SERVERSTATUSSERVICE_HPP