#ifndef SPLATOON_SERVER_SPLATOONSERVICE_HPP
#define SPLATOON_SERVER_SPLATOONSERVICE_HPP

#include <splatoon.grpc.pb.h>

#include "../../logger.hpp"
#include "../../nex/splatoon/splatoonSecure.hpp"

namespace grpcimpl::splatoon::v1 {

class SplatoonServiceImpl final : public SplatoonService::CallbackService {
public:
    explicit SplatoonServiceImpl(std::shared_ptr<Logger::Logger> logger, std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonRMC) :
                             logger(std::move(logger)),
                             splatoonRMC(std::move(splatoonRMC)) {}

    grpc::ServerUnaryReactor* SendNotification(grpc::CallbackServerContext* context,
        const SendNotificationRequest* request,
        google::protobuf::Empty* _) override;

    grpc::ServerUnaryReactor* RequestProbeInitiationExt(grpc::CallbackServerContext* context,
        const ProbeRequest* request,
        google::protobuf::Empty* _) override;

private:
    std::shared_ptr<Logger::Logger> logger;
    std::shared_ptr<nex::rmc::SplatoonSecureRMC> splatoonRMC;
};

} // namespace grpcimpl::splatoon::v1

#endif //SPLATOON_SERVER_SPLATOONSERVICE_HPP