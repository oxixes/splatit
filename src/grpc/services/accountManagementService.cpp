#include "accountManagementService.hpp"

#include "../../util/task.hpp"

using namespace async;

namespace grpcimpl::accountmanagement::v1 {

grpc::ServerUnaryReactor* AccountManagementServiceImpl::GetSecurityStatus(grpc::CallbackServerContext* context,
    const google::protobuf::Empty* _, SecurityStatus* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] GetSecurityStatus called");

    // Not implemented
    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented"));
    return reactor;
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::UpdateSecurityStatus(grpc::CallbackServerContext* context,
    const SecurityStatus* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] UpdateSecurityStatus called");

    // Not implemented
    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented"));
    return reactor;
}

Task<void> completeGetStoredAgreements(grpc::ServerUnaryReactor* reactor,
                                       GetStoredAgreementsResponse* reply, const std::shared_ptr<db::Database> db) {

    auto agreementsCmd = db::Database::craftGetAllAgreementsCommand();
    const db::Result result = co_await db->runCommand(std::move(agreementsCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    for (const auto& agreement : result.getData<std::vector<db::DBAgreementData>>()) {
        Agreement* info = reply->add_agreements();
        AgreementCreate* agreementInfo = info->mutable_createinfo();
        agreementInfo->set_type(agreement.type);
        agreementInfo->set_version(agreement.version);
        agreementInfo->set_country(agreement.country);
        agreementInfo->set_language(agreement.language);
        agreementInfo->set_languagename(agreement.languageName);
        agreementInfo->set_maintitle(agreement.mainTitle);
        agreementInfo->set_subtitle(agreement.subTitle);
        agreementInfo->set_maincontent(agreement.mainText);
        agreementInfo->set_subcontent(agreement.subText);
        agreementInfo->set_agreebuttontext(agreement.agreeText);
        agreementInfo->set_disagreebuttontext(agreement.disagreeText);

        google::protobuf::Timestamp* publishedAt = info->mutable_publishdate();
        auto duration = agreement.publishedAt.time_since_epoch();
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

        publishedAt->set_seconds(seconds);
        publishedAt->set_nanos(0);
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::GetStoredAgreements(grpc::CallbackServerContext* context,
    const google::protobuf::Empty* _, GetStoredAgreementsResponse* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] GetStoredAgreements called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    // Schedule the task to complete the request
    auto task = completeGetStoredAgreements(reactor, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completePublishAgreement(grpc::ServerUnaryReactor* reactor,
                                        const AgreementCreate* request, const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    auto insertCmd = db::Database::craftInsertOrUpdateAgreementCommand(
        request->type(),
        request->version(),
        request->country(),
        request->language(),
        request->languagename(),
        now,
        request->maintitle(),
        request->subtitle(),
        request->agreebuttontext(),
        request->disagreebuttontext(),
        request->maincontent(),
        request->subcontent()
    );

    const db::Result result = co_await db->runCommand(std::move(insertCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::PublishAgreement(grpc::CallbackServerContext *context,
    const AgreementCreate* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] PublishAgreement called "
               "for Agreement Type: " + request->type() + ", Version: " + std::to_string(request->version()) +
               ", Country: " + request->country() + ", Language: " + request->language());

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    // Schedule the task to complete the request
    auto task = completePublishAgreement(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeDeleteAgreement(grpc::ServerUnaryReactor* reactor,
                                        const AgreementDelete* request, const std::shared_ptr<db::Database> db) {
    auto deleteCmd = db::Database::craftDeleteAgreementCommand(
        request->type(),
        request->country(),
        request->language(),
        request->has_version() ? std::optional(request->version()) : std::nullopt
    );

    const db::Result result = co_await db->runCommand(std::move(deleteCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::DeleteAgreement(grpc::CallbackServerContext* context,
    const AgreementDelete* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] DeleteAgreement called "
               "for Agreement Type: " + request->type() + ", Country: " + request->country() +
               ", Language: " + request->language() + (request->has_version() ? ", Version: " + std::to_string(request->version()) : ""));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    // Schedule the task to complete the request
    auto task = completeDeleteAgreement(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

} // namespace grpcimpl::accountmanagement::v1