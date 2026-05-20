#ifndef SPLATOON_SERVER_BOSSSERVICE_HPP
#define SPLATOON_SERVER_BOSSSERVICE_HPP

#include <boss.grpc.pb.h>

#include "../../logger.hpp"
#include "../../settingsManager.hpp"
#include "../../db/database.hpp"
#include "../../http/server.hpp"

namespace grpcimpl::boss_config::v1 {

class BossServiceImpl final : public BossService::CallbackService {
public:
    explicit BossServiceImpl(std::shared_ptr<Logger::Logger> logger,
                             std::shared_ptr<SettingsManager> settingsMgr,
                             std::shared_ptr<db::Database> db,
                             std::shared_ptr<http::Server> httpServer) :
                             logger(std::move(logger)),
                             settingsMgr(std::move(settingsMgr)),
                             db(std::move(db)),
                             httpServer(std::move(httpServer)) {}

    grpc::ServerUnaryReactor* SetFestival(grpc::CallbackServerContext* context,
                                          const SetFestivalRequest* request,
                                          google::protobuf::Empty* reply) override;

    grpc::ServerUnaryReactor* SetVSSetting(grpc::CallbackServerContext* context,
                                           const SetVSSettingRequest* request,
                                           google::protobuf::Empty* reply) override;

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<SettingsManager> settingsMgr;
    std::shared_ptr<db::Database> db;
    std::shared_ptr<http::Server> httpServer;
};

} // namespace grpcimpl::boss_config::v1

#endif //SPLATOON_SERVER_BOSSSERVICE_HPP
