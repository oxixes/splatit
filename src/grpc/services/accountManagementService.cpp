#include "accountManagementService.hpp"

#include "../../util/task.hpp"
#include "../../util/globalTaskScheduler.hpp"
#include "../../crypto/tools.hpp"
#include "../../http/account/account.hpp"

using namespace async;

namespace grpcimpl::accountmanagement::v1 {

DevicePlatform dbPlatformToProto(uint32_t dbPlatform) {
    switch (dbPlatform) {
        case 1: return PLATFORM_WII_U;
        default: return PLATFORM_WII_U;
    }
}

uint32_t protoPlatformToDb(DevicePlatform protoPlatform) {
    switch (protoPlatform) {
        case PLATFORM_WII_U: return 1;
        default: return 1;
    }
}

// Helper function to convert DB region value to proto enum
DeviceRegion dbRegionToProto(uint32_t dbRegion) {
    switch (dbRegion) {
        case 1: return REGION_JPN;
        case 2: return REGION_USA;
        case 4: return REGION_EUR;
        case 8: return REGION_AUS;
        case 16: return REGION_CHN;
        case 32: return REGION_KOR;
        case 64: return REGION_TWN;
        default: return REGION_USA; // Default fallback
    }
}

// Helper function to convert proto enum to DB region value
uint32_t protoRegionToDb(DeviceRegion protoRegion) {
    switch (protoRegion) {
        case REGION_JPN: return 1;
        case REGION_USA: return 2;
        case REGION_EUR: return 4;
        case REGION_AUS: return 8;
        case REGION_CHN: return 16;
        case REGION_KOR: return 32;
        case REGION_TWN: return 64;
        default: return 2; // Default to USA
    }
}

// Helper function to convert DB status string to proto enum
DeviceStatus dbStatusToProto(const std::string& dbStatus) {
    if (dbStatus == "ACTIVE") {
        return STATUS_ACTIVE;
    }
    return STATUS_INACTIVE;
}

// Helper function to convert proto enum to DB status string
std::string protoStatusToDb(DeviceStatus protoStatus) {
    return (protoStatus == STATUS_ACTIVE) ? "ACTIVE" : "INACTIVE";
}

// Helper function to convert DB gender (0=male, 1=female) to proto enum
AccountGender dbGenderToProto(bool dbGender) {
    return dbGender ? GENDER_FEMALE : GENDER_MALE;
}

// Helper function to convert proto enum to DB gender (0=male, 1=female)
bool protoGenderToDb(AccountGender protoGender) {
    return protoGender == GENDER_FEMALE;
}

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
                                       const GetStoredAgreementsRequest* request, GetStoredAgreementsResponse* reply,
                                       const std::shared_ptr<db::Database> db) {
    std::vector<std::pair<std::string, bool>> sortBy;
    for (const int sortCriterion : request->sorting()) {
        switch (sortCriterion) {
            case SORT_BY_TYPE_ASC:
                sortBy.emplace_back("type", false);
                break;
            case SORT_BY_TYPE_DESC:
                sortBy.emplace_back("type", true);
                break;
            case SORT_BY_VERSION_ASC:
                sortBy.emplace_back("version", false);
                break;
            case SORT_BY_VERSION_DESC:
                sortBy.emplace_back("version", true);
                break;
            case SORT_BY_COUNTRY_ASC:
                sortBy.emplace_back("country", false);
                break;
            case SORT_BY_COUNTRY_DESC:
                sortBy.emplace_back("country", true);
                break;
            case SORT_BY_LANGUAGE_ASC:
                sortBy.emplace_back("language", false);
                break;
            case SORT_BY_LANGUAGE_DESC:
                sortBy.emplace_back("language", true);
                break;
            default:
                break;
        }
    }

    auto agreementsCmd = db::Database::craftGetAllAgreementsCommand(
        request->has_type() ? std::optional(request->type()) : std::nullopt,
        request->has_country() ? std::optional(request->country()) : std::nullopt,
        request->has_language() ? std::optional(request->language()) : std::nullopt,
        request->has_version() ? std::optional(static_cast<int>(request->version())) : std::nullopt,
        std::move(sortBy),
        request->has_pagerequest() ? request->pagerequest().pagesize() : std::numeric_limits<uint32_t>::max(),
        request->has_pagerequest() ? request->pagerequest().pagenumber() : 0
    );

    const db::Result result = co_await db->runCommand(std::move(agreementsCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    auto countCmd = db::Database::craftCountAgreementsCommand(
        request->has_type() ? std::optional(request->type()) : std::nullopt,
        request->has_country() ? std::optional(request->country()) : std::nullopt,
        request->has_language() ? std::optional(request->language()) : std::nullopt,
        request->has_version() ? std::optional(static_cast<int>(request->version())) : std::nullopt
    );

    const db::Result countResult = co_await db->runCommand(std::move(countCmd));
    if (countResult.getStatus() != db::DBResultStatus::SUCCESS || !countResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    const auto totalCount = countResult.getData<int64_t>();
    reply->mutable_pageresponse()->set_totalitems(static_cast<uint64_t>(totalCount));

    uint64_t totalPages = 1;
    if (request->has_pagerequest() && request->pagerequest().pagesize() > 0) {
        totalPages = (static_cast<uint64_t>(totalCount) + request->pagerequest().pagesize() - 1) / request->pagerequest().pagesize();
    }
    reply->mutable_pageresponse()->set_totalpages(totalPages);

    if (totalPages > 0 && request->has_pagerequest()) {
        reply->mutable_pageresponse()->set_currentpage(std::min(request->pagerequest().pagenumber(), totalPages - 1));
    } else {
        reply->mutable_pageresponse()->set_currentpage(0);
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
    const GetStoredAgreementsRequest* request, GetStoredAgreementsResponse* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] GetStoredAgreements called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    // Schedule the task to complete the request
    auto task = completeGetStoredAgreements(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completePublishAgreement(grpc::ServerUnaryReactor* reactor,
                                        const AgreementCreate* request, const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    auto insertCmd = db::Database::craftInsertOrUpdateAgreementCommand(
        request->type(),
        static_cast<int>(request->version()),
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

// ==================== Device Management ====================

Task<void> completeCreateDevice(grpc::ServerUnaryReactor* reactor, const DeviceCreate* request,
                                Device* reply, const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Generate a random device ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(100000000, 999999999);

    uint32_t deviceId;
    do {
        deviceId = dis(gen);
        auto checkCmd = db::Database::craftGetDeviceCommand(deviceId);
        const db::Result checkResult = co_await db->runCommand(std::move(checkCmd));
        if (checkResult.getStatus() != db::DBResultStatus::SUCCESS) {
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
            co_return;
        }

        if (!checkResult.hasData()) {
            // Device ID is unique
            break;
        }
    } while (true);

    auto insertCmd = db::Database::craftInsertOrUpdateDeviceCommand(
        deviceId,
        request->language(),
        protoPlatformToDb(request->platform()),
        protoRegionToDb(request->region()),
        request->serialnumber(),
        request->systemversion(),
        request->type(),
        request->updatedby(),
        request->status(),
        false,
        now
    );

    const db::Result result = co_await db->runCommand(std::move(insertCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Return the created device
    reply->set_id(deviceId);
    reply->set_language(request->language());
    reply->set_platform(request->platform());
    reply->set_region(request->region());
    reply->set_serialnumber(request->serialnumber());
    reply->set_systemversion(request->systemversion());
    reply->set_type(request->type());
    reply->set_updatedby(request->updatedby());
    reply->set_banned(false);
    reply->set_status(request->status());

    auto* timestamp = reply->mutable_lastupdated();
    timestamp->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    timestamp->set_nanos(0);

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::CreateDevice(grpc::CallbackServerContext* context,
    const DeviceCreate* request, Device* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] CreateDevice called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeCreateDevice(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeGetDevice(grpc::ServerUnaryReactor* reactor, const DeviceGetRequest* request,
                             Device* reply, const std::shared_ptr<db::Database> db) {
    auto getCmd = db::Database::craftGetDeviceCommand(request->id());

    const db::Result result = co_await db->runCommand(std::move(getCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!result.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Device not found"));
        co_return;
    }

    const auto deviceData = result.getData<db::DBDeviceData>();

    reply->set_id(deviceData.deviceId);
    reply->set_language(deviceData.language);
    reply->set_platform(dbPlatformToProto(deviceData.platformId));
    reply->set_region(dbRegionToProto(deviceData.region));
    reply->set_serialnumber(deviceData.serialNumber);
    reply->set_systemversion(deviceData.systemVersion);
    reply->set_type(deviceData.type);
    reply->set_updatedby(deviceData.updatedBy);
    reply->set_banned(deviceData.banned);
    reply->set_status(deviceData.status);

    auto* timestamp = reply->mutable_lastupdated();
    timestamp->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(deviceData.lastUpdated.time_since_epoch()).count());
    timestamp->set_nanos(0);

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::GetDevice(grpc::CallbackServerContext* context,
    const DeviceGetRequest* request, Device* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] GetDevice called for ID: " +
               std::to_string(request->id()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeGetDevice(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeListDevices(grpc::ServerUnaryReactor* reactor, const ListDevicesRequest* request,
                                ListDevicesResponse* reply, const std::shared_ptr<db::Database> db) {
    std::vector<std::pair<std::string, bool>> sortBy;
    for (const int sortCriterion : request->sorting()) {
        switch (sortCriterion) {
            case SORT_DEVICE_BY_ID_ASC:
                sortBy.emplace_back("id", false);
                break;
            case SORT_DEVICE_BY_ID_DESC:
                sortBy.emplace_back("id", true);
                break;
            case SORT_DEVICE_BY_PLATFORM_ASC:
                sortBy.emplace_back("platform", false);
                break;
            case SORT_DEVICE_BY_PLATFORM_DESC:
                sortBy.emplace_back("platform", true);
                break;
            case SORT_DEVICE_BY_REGION_ASC:
                sortBy.emplace_back("region", false);
                break;
            case SORT_DEVICE_BY_REGION_DESC:
                sortBy.emplace_back("region", true);
                break;
            case SORT_DEVICE_BY_SERIAL_ASC:
                sortBy.emplace_back("serial", false);
                break;
            case SORT_DEVICE_BY_SERIAL_DESC:
                sortBy.emplace_back("serial", true);
                break;
            case SORT_DEVICE_BY_LAST_UPDATED_ASC:
                sortBy.emplace_back("lastUpdated", false);
                break;
            case SORT_DEVICE_BY_LAST_UPDATED_DESC:
                sortBy.emplace_back("lastUpdated", true);
                break;
            default:
                break;
        }
    }

    auto listCmd = db::Database::craftListDevicesCommand(
        request->has_platform() ? std::optional(protoPlatformToDb(request->platform())) : std::nullopt,
        request->has_region() ? std::optional(protoRegionToDb(request->region())) : std::nullopt,
        request->has_banned() ? std::optional(request->banned()) : std::nullopt,
        request->has_serialnumber() ? std::optional(request->serialnumber()) : std::nullopt,
        request->has_type() ? std::optional(request->type()) : std::nullopt,
        std::move(sortBy),
        request->has_pagerequest() ? request->pagerequest().pagesize() : std::numeric_limits<uint32_t>::max(),
        request->has_pagerequest() ? request->pagerequest().pagenumber() : 0
    );

    const db::Result result = co_await db->runCommand(std::move(listCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    auto countCmd = db::Database::craftCountDevicesCommand(
        request->has_platform() ? std::optional(protoPlatformToDb(request->platform())) : std::nullopt,
        request->has_region() ? std::optional(protoRegionToDb(request->region())) : std::nullopt,
        request->has_banned() ? std::optional(request->banned()) : std::nullopt,
        request->has_serialnumber() ? std::optional(request->serialnumber()) : std::nullopt,
        request->has_type() ? std::optional(request->type()) : std::nullopt
    );

    const db::Result countResult = co_await db->runCommand(std::move(countCmd));
    if (countResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    const auto totalCount = countResult.getData<int64_t>();
    reply->mutable_pageresponse()->set_totalitems(static_cast<uint64_t>(totalCount));

    uint64_t totalPages = 1;
    if (request->has_pagerequest() && request->pagerequest().pagesize() > 0) {
        totalPages = (static_cast<uint64_t>(totalCount) + request->pagerequest().pagesize() - 1) / request->pagerequest().pagesize();
    }
    reply->mutable_pageresponse()->set_totalpages(totalPages);

    if (totalPages > 0 && request->has_pagerequest()) {
        reply->mutable_pageresponse()->set_currentpage(std::min(request->pagerequest().pagenumber(), totalPages - 1));
    } else {
        reply->mutable_pageresponse()->set_currentpage(0);
    }

    if (result.hasData()) {
        for (const auto& deviceData : result.getData<std::vector<db::DBDeviceData>>()) {
            Device* device = reply->add_devices();
            device->set_id(deviceData.deviceId);
            device->set_language(deviceData.language);
            device->set_platform(dbPlatformToProto(deviceData.platformId));
            device->set_region(dbRegionToProto(deviceData.region));
            device->set_serialnumber(deviceData.serialNumber);
            device->set_systemversion(deviceData.systemVersion);
            device->set_type(deviceData.type);
            device->set_updatedby(deviceData.updatedBy);
            device->set_banned(deviceData.banned);
            device->set_status(deviceData.status);

            auto* timestamp = device->mutable_lastupdated();
            timestamp->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(deviceData.lastUpdated.time_since_epoch()).count());
            timestamp->set_nanos(0);
        }
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::ListDevices(grpc::CallbackServerContext* context,
    const ListDevicesRequest* request, ListDevicesResponse* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] ListDevices called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeListDevices(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeUpdateDevice(grpc::ServerUnaryReactor* reactor, const DeviceUpdate* request,
                                Device* reply, const std::shared_ptr<db::Database> db) {
    // First, get the current device
    auto getCmd = db::Database::craftGetDeviceCommand(request->id());
    const db::Result getResult = co_await db->runCommand(std::move(getCmd));

    if (getResult.getStatus() != db::DBResultStatus::SUCCESS || !getResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Device not found"));
        co_return;
    }

    const auto currentDevice = getResult.getData<db::DBDeviceData>();
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Update with new values or keep existing ones
    auto updateCmd = db::Database::craftInsertOrUpdateDeviceCommand(
        request->id(),
        request->has_language() ? request->language() : currentDevice.language,
        request->has_platform() ? protoPlatformToDb(request->platform()) : currentDevice.platformId,
        request->has_region() ? protoRegionToDb(request->region()) : currentDevice.region,
        request->has_serialnumber() ? request->serialnumber() : currentDevice.serialNumber,
        request->has_systemversion() ? request->systemversion() : currentDevice.systemVersion,
        request->has_type() ? request->type() : currentDevice.type,
        request->has_updatedby() ? request->updatedby() : currentDevice.updatedBy,
        request->has_status() ? request->status() : currentDevice.status,
        request->has_banned() ? request->banned() : currentDevice.banned,
        now
    );

    const db::Result result = co_await db->runCommand(std::move(updateCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Return updated device
    reply->set_id(request->id());
    reply->set_language(request->has_language() ? request->language() : currentDevice.language);
    reply->set_platform(request->has_platform() ? request->platform() : dbPlatformToProto(currentDevice.platformId));
    reply->set_region(request->has_region() ? request->region() : dbRegionToProto(currentDevice.region));
    reply->set_serialnumber(request->has_serialnumber() ? request->serialnumber() : currentDevice.serialNumber);
    reply->set_systemversion(request->has_systemversion() ? request->systemversion() : currentDevice.systemVersion);
    reply->set_type(request->has_type() ? request->type() : currentDevice.type);
    reply->set_updatedby(request->has_updatedby() ? request->updatedby() : currentDevice.updatedBy);
    reply->set_banned(request->has_banned() ? request->banned() : currentDevice.banned);
    reply->set_status(request->has_status() ? request->status() : currentDevice.status);

    auto* timestamp = reply->mutable_lastupdated();
    timestamp->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    timestamp->set_nanos(0);

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::UpdateDevice(grpc::CallbackServerContext* context,
    const DeviceUpdate* request, Device* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] UpdateDevice called for ID: " +
               std::to_string(request->id()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeUpdateDevice(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeDeleteDevice(grpc::ServerUnaryReactor* reactor, const DeviceDeleteRequest* request,
                                const std::shared_ptr<db::Database> db) {
    // Create a database session for transaction support
    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start database transaction"));
        co_return;
    }

    // Delete all ownerships for this device
    auto deleteOwnershipsCmd = db::Database::craftDeleteDeviceOwnershipsCommand(request->id());
    db::Result deleteOwnershipsResults = co_await session->runCommand(std::move(deleteOwnershipsCmd));
    if (deleteOwnershipsResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting device ownerships"));
        co_return;
    }

    // Delete all device attributes for this device
    auto deleteAttributesCmd = db::Database::craftDeleteDeviceAttributesCommand(request->id());
    db::Result deleteAttributesResults = co_await session->runCommand(std::move(deleteAttributesCmd));
    if (deleteAttributesResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting device attributes"));
        co_return;
    }

    // Delete the device
    auto deleteDeviceCmd = db::Database::craftDeleteDeviceCommand(request->id());
    const db::Result result = co_await session->runCommand(std::move(deleteDeviceCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting device"));
        co_return;
    }

    // Commit the transaction
    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to commit transaction"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::DeleteDevice(grpc::CallbackServerContext* context,
    const DeviceDeleteRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] DeleteDevice called for ID: " +
               std::to_string(request->id()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeDeleteDevice(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

// ==================== Account Management ====================

Task<void> completeCreateAccount(grpc::ServerUnaryReactor* reactor, const AccountCreate* request,
                                Account* reply, const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Create a database session for transaction support
    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start database transaction"));
        co_return;
    }

    // Check if the username is already taken
    auto checkUsernameCmd = db::Database::craftGetUserByUsernameCommand(request->username());
    const db::Result checkUsernameResult = co_await session->runCommand(std::move(checkUsernameCmd));
    if (checkUsernameResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error checking username"));
        co_return;
    }

    if (checkUsernameResult.hasData()) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "Username already taken"));
        co_return;
    }

    // Create email
    auto insertEmailCmd = db::Database::craftInsertOrUpdateEmailCommand(
        std::nullopt, // Let DB generate ID
        request->email().address(),
        request->email().parent(),
        request->email().primary(),
        request->email().reachable(),
        request->email().type(),
        request->email().updatedby(),
        request->email().validated(),
        now,
        "" // validation code
    );

    const db::Result emailResult = co_await session->runCommand(std::move(insertEmailCmd));
    if (emailResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error creating email"));
        co_return;
    }

    auto emailId = emailResult.getData<int64_t>();

    // Create mii
    std::string hash = crypto::genRandomString(13, "abcdefghijklmnopqrstuvwxyz0123456789");

    auto insertMiiCmd = db::Database::craftInsertOrUpdateMiiCommand(
        std::nullopt, // Let DB generate ID
        hash,
        request->mii().name(),
        request->mii().primary(),
        request->mii().data()
    );

    const db::Result miiResult = co_await session->runCommand(std::move(insertMiiCmd));
    if (miiResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error creating mii"));
        co_return;
    }

    auto miiId = miiResult.getData<int64_t>();

    // Create user profile
    auto insertProfileCmd = db::Database::craftInsertProfileCommand(
        request->username(),
        "x", // password is updated later
        emailId,
        miiId,
        protoGenderToDb(request->gender()),
        static_cast<int64_t>(request->region()),
        request->timezone(),
        request->language(),
        true, // active by default
        false, // marketing
        false, // offDevice
        "", // birthdate
        "", // country
        now,
        now
    );

    const db::Result result = co_await session->runCommand(std::move(insertProfileCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error creating profile"));
        co_return;
    }

    auto newPid = static_cast<uint32_t>(result.getData<int64_t>());

    std::string hashedPassword;
    try {
        std::string nintendoPasswordHash = crypto::genNintendoPasswordHash(newPid, request->password());
        std::string salt = crypto::genSalt();
        hashedPassword = crypto::hashPassword(nintendoPasswordHash, salt);
    } catch (const std::exception&) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Error hashing password"));
        co_return;
    }

    auto updatePasswordCmd = db::Database::craftUpdateUserProfileCommand(
        newPid,
        std::nullopt, hashedPassword, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt);
    const db::Result updatePasswordResult = co_await session->runCommand(std::move(updatePasswordCmd));
    if (updatePasswordResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error updating password"));
        co_return;
    }

    // Commit the transaction
    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to commit transaction"));
        co_return;
    }

    // Return the created account
    reply->set_pid(newPid);
    reply->set_username(request->username());
    reply->set_gender(request->gender());
    reply->set_region(request->region());
    reply->set_timezone(request->timezone());
    reply->set_language(request->language());
    reply->set_active(true);

    if (request->has_email()) {
        auto* email = reply->mutable_primaryemail();
        email->set_id(emailId);
        email->set_address(request->email().address());
        email->set_parent(request->email().parent());
        email->set_primary(request->email().primary());
        email->set_reachable(request->email().reachable());
        email->set_type(request->email().type());
        email->set_updatedby(request->email().updatedby());
        email->set_validated(request->email().validated());

        auto* validatedAt = email->mutable_validatedat();
        validatedAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
        validatedAt->set_nanos(0);
    }

    if (request->has_mii()) {
        auto* mii = reply->mutable_mii();
        mii->set_id(miiId);
        mii->set_hash(std::to_string(std::hash<std::string>{}(request->mii().data())));
        mii->set_name(request->mii().name());
        mii->set_primary(request->mii().primary());
        mii->set_data(request->mii().data());
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::CreateAccount(grpc::CallbackServerContext* context,
    const AccountCreate* request, Account* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] CreateAccount called for username: " +
               request->username());

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeCreateAccount(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeGetAccount(grpc::ServerUnaryReactor* reactor, const AccountGetRequest* request,
                              Account* reply, const std::shared_ptr<db::Database> db) {
    auto getCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result result = co_await db->runCommand(std::move(getCmd));

    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    const auto profileData = result.getData<db::DBUserProfileData>();

    reply->set_pid(profileData.pid);
    reply->set_username(profileData.username);
    reply->set_gender(dbGenderToProto(profileData.gender));
    reply->set_region(profileData.region);
    reply->set_timezone(profileData.tz);
    reply->set_language(profileData.language);
    reply->set_active(profileData.active);
    reply->set_marketing(profileData.marketing);
    reply->set_offdevice(profileData.offDevice);
    reply->set_birthdate(profileData.birthdate);
    reply->set_country(profileData.country);

    // Set created timestamp
    auto* createdTs = reply->mutable_created();
    createdTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.created.time_since_epoch()).count());
    createdTs->set_nanos(0);

    // Set updated timestamp
    auto* updatedTs = reply->mutable_updated();
    updatedTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.updated.time_since_epoch()).count());
    updatedTs->set_nanos(0);

    // Set email
    auto* email = reply->mutable_primaryemail();
    email->set_id(profileData.emailId);
    email->set_address(profileData.email);
    email->set_parent(profileData.emailParent);
    email->set_primary(profileData.emailPrimary);
    email->set_reachable(profileData.emailReachable);
    email->set_type(profileData.emailType);
    email->set_updatedby(profileData.emailUpdatedBy);
    email->set_validated(profileData.emailValidated);
    email->set_validationcode(profileData.emailValidationCode);

    auto* emailValidatedAt = email->mutable_validatedat();
    emailValidatedAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.emailValidatedDate.time_since_epoch()).count());
    emailValidatedAt->set_nanos(0);

    // Set mii
    auto* mii = reply->mutable_mii();
    mii->set_id(profileData.miiId);
    mii->set_hash(profileData.miiHash);
    mii->set_name(profileData.miiName);
    mii->set_primary(profileData.miiPrimary);
    mii->set_data(profileData.miiData);

    // Get signed agreements
    auto agreementsCmd = db::Database::craftGetSignedAgreementsCommand(request->pid());
    const db::Result agreementsResult = co_await db->runCommand(std::move(agreementsCmd));

    if (agreementsResult.getStatus() == db::DBResultStatus::SUCCESS && agreementsResult.hasData()) {
        for (const auto& agreement : agreementsResult.getData<std::vector<db::DBUserAgreementData>>()) {
            auto* agr = reply->add_signedagreements();
            agr->set_type(agreement.type);
            agr->set_version(agreement.version);
            agr->set_country(agreement.country);
        }
    }

    // Get ownerships
    auto ownershipsCmd = db::Database::craftGetOwnershipsCommand(request->pid());
    const db::Result ownershipsResult = co_await db->runCommand(std::move(ownershipsCmd));

    if (ownershipsResult.getStatus() == db::DBResultStatus::SUCCESS && ownershipsResult.hasData()) {
        for (const auto& ownership : ownershipsResult.getData<std::vector<db::DBOwnershipData>>()) {
            auto* own = reply->add_owneddevices();

            // Get device details
            auto deviceCmd = db::Database::craftGetDeviceCommand(ownership.deviceId);
            const db::Result deviceResult = co_await db->runCommand(std::move(deviceCmd));

            if (deviceResult.getStatus() == db::DBResultStatus::SUCCESS && deviceResult.hasData()) {
                const auto deviceData = deviceResult.getData<db::DBDeviceData>();

                auto* device = own->mutable_device();
                device->set_id(deviceData.deviceId);
                device->set_language(deviceData.language);
                device->set_platform(dbPlatformToProto(deviceData.platformId));
                device->set_region(dbRegionToProto(deviceData.region));
                device->set_serialnumber(deviceData.serialNumber);
                device->set_systemversion(deviceData.systemVersion);
                device->set_type(deviceData.type);
                device->set_updatedby(deviceData.updatedBy);
                device->set_banned(deviceData.banned);
                device->set_status(deviceData.status);

                auto* timestamp = device->mutable_lastupdated();
                timestamp->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(deviceData.lastUpdated.time_since_epoch()).count());
                timestamp->set_nanos(0);
            }

            own->set_status(dbStatusToProto(ownership.status));

            auto* lastUpdated = own->mutable_lastupdated();
            lastUpdated->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(ownership.lastUpdated.time_since_epoch()).count());
            lastUpdated->set_nanos(0);

            // Get device attributes for this ownership
            auto attrsCmd = db::Database::craftGetDeviceAttributesCommand(request->pid(), ownership.deviceId);
            const db::Result attrsResult = co_await db->runCommand(std::move(attrsCmd));

            if (attrsResult.getStatus() == db::DBResultStatus::SUCCESS && attrsResult.hasData()) {
                for (const auto& attr : attrsResult.getData<std::vector<db::DBDeviceAttributeData>>()) {
                    auto* attribute = own->add_accountdeviceattributes();
                    attribute->set_name(attr.name);
                    attribute->set_value(attr.value);

                    auto* createdAt = attribute->mutable_createdat();
                    createdAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(attr.createdDate.time_since_epoch()).count());
                    createdAt->set_nanos(0);
                }
            }
        }
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::GetAccount(grpc::CallbackServerContext* context,
    const AccountGetRequest* request, Account* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] GetAccount called for PID: " +
               std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeGetAccount(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeGetAccountByUsername(grpc::ServerUnaryReactor* reactor, const AccountGetByUsernameRequest* request,
                                       Account* reply, const std::shared_ptr<db::Database> db) {
    // First get the user to find their PID
    auto getUserCmd = db::Database::craftGetUserByUsernameCommand(request->username());
    const db::Result userResult = co_await db->runCommand(std::move(getUserCmd));

    if (userResult.getStatus() != db::DBResultStatus::SUCCESS || !userResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    const auto userData = userResult.getData<db::DBUserData>();

    // Now get the full profile
    auto getCmd = db::Database::craftGetUserProfileCommand(userData.pid);
    const db::Result result = co_await db->runCommand(std::move(getCmd));

    if (result.getStatus() != db::DBResultStatus::SUCCESS || !result.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account profile not found"));
        co_return;
    }

    const auto profileData = result.getData<db::DBUserProfileData>();

    reply->set_pid(profileData.pid);
    reply->set_username(profileData.username);
    reply->set_gender(dbGenderToProto(profileData.gender));
    reply->set_region(profileData.region);
    reply->set_timezone(profileData.tz);
    reply->set_language(profileData.language);
    reply->set_active(profileData.active);
    reply->set_marketing(profileData.marketing);
    reply->set_offdevice(profileData.offDevice);
    reply->set_birthdate(profileData.birthdate);
    reply->set_country(profileData.country);

    // Set created timestamp
    auto* createdTs = reply->mutable_created();
    createdTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.created.time_since_epoch()).count());
    createdTs->set_nanos(0);

    // Set updated timestamp
    auto* updatedTs = reply->mutable_updated();
    updatedTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.updated.time_since_epoch()).count());
    updatedTs->set_nanos(0);

    // Set email
    auto* email = reply->mutable_primaryemail();
    email->set_id(profileData.emailId);
    email->set_address(profileData.email);
    email->set_parent(profileData.emailParent);
    email->set_primary(profileData.emailPrimary);
    email->set_reachable(profileData.emailReachable);
    email->set_type(profileData.emailType);
    email->set_updatedby(profileData.emailUpdatedBy);
    email->set_validated(profileData.emailValidated);
    email->set_validationcode(profileData.emailValidationCode);

    auto* emailValidatedAt = email->mutable_validatedat();
    emailValidatedAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.emailValidatedDate.time_since_epoch()).count());
    emailValidatedAt->set_nanos(0);

    // Set mii
    auto* mii = reply->mutable_mii();
    mii->set_id(profileData.miiId);
    mii->set_hash(profileData.miiHash);
    mii->set_name(profileData.miiName);
    mii->set_primary(profileData.miiPrimary);
    mii->set_data(profileData.miiData);

    // Get signed agreements
    auto agreementsCmd = db::Database::craftGetSignedAgreementsCommand(userData.pid);
    const db::Result agreementsResult = co_await db->runCommand(std::move(agreementsCmd));

    if (agreementsResult.getStatus() == db::DBResultStatus::SUCCESS && agreementsResult.hasData()) {
        for (const auto& agreement : agreementsResult.getData<std::vector<db::DBUserAgreementData>>()) {
            auto* agr = reply->add_signedagreements();
            agr->set_type(agreement.type);
            agr->set_version(agreement.version);
            agr->set_country(agreement.country);
        }
    }

    // Get ownerships
    auto ownershipsCmd = db::Database::craftGetOwnershipsCommand(userData.pid);
    const db::Result ownershipsResult = co_await db->runCommand(std::move(ownershipsCmd));

    if (ownershipsResult.getStatus() == db::DBResultStatus::SUCCESS && ownershipsResult.hasData()) {
        for (const auto& ownership : ownershipsResult.getData<std::vector<db::DBOwnershipData>>()) {
            auto* own = reply->add_owneddevices();

            // Get device details
            auto deviceCmd = db::Database::craftGetDeviceCommand(ownership.deviceId);
            const db::Result deviceResult = co_await db->runCommand(std::move(deviceCmd));

            if (deviceResult.getStatus() == db::DBResultStatus::SUCCESS && deviceResult.hasData()) {
                const auto deviceData = deviceResult.getData<db::DBDeviceData>();

                auto* device = own->mutable_device();
                device->set_id(deviceData.deviceId);
                device->set_language(deviceData.language);
                device->set_platform(dbPlatformToProto(deviceData.platformId));
                device->set_region(dbRegionToProto(deviceData.region));
                device->set_serialnumber(deviceData.serialNumber);
                device->set_systemversion(deviceData.systemVersion);
                device->set_type(deviceData.type);
                device->set_updatedby(deviceData.updatedBy);
                device->set_banned(deviceData.banned);
                device->set_status(deviceData.status);

                auto* timestamp = device->mutable_lastupdated();
                timestamp->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(deviceData.lastUpdated.time_since_epoch()).count());
                timestamp->set_nanos(0);
            }

            own->set_status(dbStatusToProto(ownership.status));

            auto* lastUpdated = own->mutable_lastupdated();
            lastUpdated->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(ownership.lastUpdated.time_since_epoch()).count());
            lastUpdated->set_nanos(0);

            // Get device attributes for this ownership
            auto attrsCmd = db::Database::craftGetDeviceAttributesCommand(userData.pid, ownership.deviceId);
            const db::Result attrsResult = co_await db->runCommand(std::move(attrsCmd));

            if (attrsResult.getStatus() == db::DBResultStatus::SUCCESS && attrsResult.hasData()) {
                for (const auto& attr : attrsResult.getData<std::vector<db::DBDeviceAttributeData>>()) {
                    auto* attribute = own->add_accountdeviceattributes();
                    attribute->set_name(attr.name);
                    attribute->set_value(attr.value);

                    auto* createdAt = attribute->mutable_createdat();
                    createdAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(attr.createdDate.time_since_epoch()).count());
                    createdAt->set_nanos(0);
                }
            }
        }
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::GetAccountByUsername(grpc::CallbackServerContext* context,
    const AccountGetByUsernameRequest* request, Account* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] GetAccountByUsername called for username: " +
               request->username());

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeGetAccountByUsername(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeListAccounts(grpc::ServerUnaryReactor* reactor, const ListAccountsRequest* request,
                                ListAccountsResponse* reply, const std::shared_ptr<db::Database> db) {
    std::vector<std::pair<std::string, bool>> sortBy;
    for (const int sortCriterion : request->sorting()) {
        switch (sortCriterion) {
            case SORT_ACCOUNT_BY_PID_ASC:
                sortBy.emplace_back("pid", false);
                break;
            case SORT_ACCOUNT_BY_PID_DESC:
                sortBy.emplace_back("pid", true);
                break;
            case SORT_ACCOUNT_BY_USERNAME_ASC:
                sortBy.emplace_back("username", false);
                break;
            case SORT_ACCOUNT_BY_USERNAME_DESC:
                sortBy.emplace_back("username", true);
                break;
            case SORT_ACCOUNT_BY_REGION_ASC:
                sortBy.emplace_back("region", false);
                break;
            case SORT_ACCOUNT_BY_REGION_DESC:
                sortBy.emplace_back("region", true);
                break;
            default:
                break;
        }
    }

    auto listCmd = db::Database::craftListAccountsCommand(
        request->has_username() ? std::optional(request->username()) : std::nullopt,
        request->has_gender() ? std::optional(protoGenderToDb(request->gender())) : std::nullopt,
        request->has_region() ? std::optional(request->region()) : std::nullopt,
        request->has_active() ? std::optional(request->active()) : std::nullopt,
        std::move(sortBy),
        request->has_pagerequest() ? request->pagerequest().pagesize() : std::numeric_limits<uint32_t>::max(),
        request->has_pagerequest() ? request->pagerequest().pagenumber() : 0
    );

    const db::Result result = co_await db->runCommand(std::move(listCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    auto countCmd = db::Database::craftCountAccountsCommand(
        request->has_username() ? std::optional(request->username()) : std::nullopt,
        request->has_gender() ? std::optional(protoGenderToDb(request->gender())) : std::nullopt,
        request->has_region() ? std::optional(request->region()) : std::nullopt,
        request->has_active() ? std::optional(request->active()) : std::nullopt
    );

    const db::Result countResult = co_await db->runCommand(std::move(countCmd));
    if (countResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    const auto totalCount = countResult.getData<int64_t>();
    reply->mutable_pageresponse()->set_totalitems(static_cast<uint64_t>(totalCount));

    uint64_t totalPages = 1;
    if (request->has_pagerequest() && request->pagerequest().pagesize() > 0) {
        totalPages = (static_cast<uint64_t>(totalCount) + request->pagerequest().pagesize() - 1) / request->pagerequest().pagesize();
    }
    reply->mutable_pageresponse()->set_totalpages(totalPages);

    if (totalPages > 0 && request->has_pagerequest()) {
        reply->mutable_pageresponse()->set_currentpage(std::min(request->pagerequest().pagenumber(), totalPages - 1));
    } else {
        reply->mutable_pageresponse()->set_currentpage(0);
    }

    if (result.hasData()) {
        for (const auto& profileData : result.getData<std::vector<db::DBUserProfileData>>()) {
            Account* account = reply->add_accounts();
            account->set_pid(profileData.pid);
            account->set_username(profileData.username);
            account->set_gender(dbGenderToProto(profileData.gender));
            account->set_region(profileData.region);
            account->set_timezone(profileData.tz);
            account->set_language(profileData.language);
            account->set_active(profileData.active);
            account->set_marketing(profileData.marketing);
            account->set_offdevice(profileData.offDevice);
            account->set_birthdate(profileData.birthdate);
            account->set_country(profileData.country);

            // Set created timestamp
            auto* createdTs = account->mutable_created();
            createdTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.created.time_since_epoch()).count());
            createdTs->set_nanos(0);

            // Set updated timestamp
            auto* updatedTs = account->mutable_updated();
            updatedTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.updated.time_since_epoch()).count());
            updatedTs->set_nanos(0);

            // Set email
            auto* email = account->mutable_primaryemail();
            email->set_id(profileData.emailId);
            email->set_address(profileData.email);
            email->set_parent(profileData.emailParent);
            email->set_primary(profileData.emailPrimary);
            email->set_reachable(profileData.emailReachable);
            email->set_type(profileData.emailType);
            email->set_updatedby(profileData.emailUpdatedBy);
            email->set_validated(profileData.emailValidated);
            email->set_validationcode(profileData.emailValidationCode);

            auto* emailValidatedAt = email->mutable_validatedat();
            emailValidatedAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.emailValidatedDate.time_since_epoch()).count());
            emailValidatedAt->set_nanos(0);

            // Set mii
            auto* mii = account->mutable_mii();
            mii->set_id(profileData.miiId);
            mii->set_hash(profileData.miiHash);
            mii->set_name(profileData.miiName);
            mii->set_primary(profileData.miiPrimary);
            mii->set_data(profileData.miiData);
        }
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::ListAccounts(grpc::CallbackServerContext* context,
    const ListAccountsRequest* request, ListAccountsResponse* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] ListAccounts called");

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeListAccounts(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeUpdateAccount(grpc::ServerUnaryReactor* reactor, const AccountUpdate* request,
                                Account* reply, const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Check if account exists
    auto checkCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result checkResult = co_await db->runCommand(std::move(checkCmd));
    if (checkResult.getStatus() != db::DBResultStatus::SUCCESS || !checkResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    // Check if username is being changed and if the new one is already taken
    if (request->has_username()) {
        auto checkUsernameCmd = db::Database::craftGetUserByUsernameCommand(request->username());
        const db::Result checkUsernameResult = co_await db->runCommand(std::move(checkUsernameCmd));
        if (checkUsernameResult.getStatus() != db::DBResultStatus::SUCCESS) {
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error checking username"));
            co_return;
        }

        if (checkUsernameResult.hasData()) {
            const auto userData = checkUsernameResult.getData<db::DBUserData>();
            if (userData.pid != request->pid()) {
                reactor->Finish(grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "Username already taken"));
                co_return;
            }
        }
    }

    // Hash password if being changed
    std::optional<std::string> hashedPassword = std::nullopt;
    if (request->has_password()) {
        try {
            std::string nintendoPasswordHash = crypto::genNintendoPasswordHash(request->pid(), request->password());
            std::string salt = crypto::genSalt();
            hashedPassword = crypto::hashPassword(nintendoPasswordHash, salt);
        } catch (const std::exception&) {
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Error hashing password"));
            co_return;
        }
    }

    auto updateCmd = db::Database::craftUpdateUserProfileCommand(
        request->pid(),
        request->has_username() ? std::optional(request->username()) : std::nullopt,
        hashedPassword,
        std::nullopt, // emailId - not changed here
        std::nullopt, // miiId - not changed here
        request->has_gender() ? std::optional(protoGenderToDb(request->gender())) : std::nullopt,
        request->has_region() ? std::optional(static_cast<int64_t>(request->region())) : std::nullopt,
        request->has_timezone() ? std::optional(request->timezone()) : std::nullopt,
        request->has_language() ? std::optional(request->language()) : std::nullopt,
        request->has_active() ? std::optional(request->active()) : std::nullopt,
        request->has_marketing() ? std::optional(request->marketing()) : std::nullopt,
        request->has_offdevice() ? std::optional(request->offdevice()) : std::nullopt,
        request->has_birthdate() ? std::optional(request->birthdate()) : std::nullopt,
        request->has_country() ? std::optional(request->country()) : std::nullopt,
        std::nullopt, // created
        std::optional(now) // updated
    );

    const db::Result result = co_await db->runCommand(std::move(updateCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Get the updated account to return
    auto getCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result getResult = co_await db->runCommand(std::move(getCmd));

    if (getResult.getStatus() != db::DBResultStatus::SUCCESS || !getResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    const auto profileData = getResult.getData<db::DBUserProfileData>();

    reply->set_pid(profileData.pid);
    reply->set_username(profileData.username);
    reply->set_gender(dbGenderToProto(profileData.gender));
    reply->set_region(profileData.region);
    reply->set_timezone(profileData.tz);
    reply->set_language(profileData.language);
    reply->set_active(profileData.active);
    reply->set_marketing(profileData.marketing);
    reply->set_offdevice(profileData.offDevice);
    reply->set_birthdate(profileData.birthdate);
    reply->set_country(profileData.country);

    // Set created timestamp
    auto* createdTs = reply->mutable_created();
    createdTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.created.time_since_epoch()).count());
    createdTs->set_nanos(0);

    // Set updated timestamp
    auto* updatedTs = reply->mutable_updated();
    updatedTs->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.updated.time_since_epoch()).count());
    updatedTs->set_nanos(0);

    // Set email
    auto* email = reply->mutable_primaryemail();
    email->set_id(profileData.emailId);
    email->set_address(profileData.email);
    email->set_parent(profileData.emailParent);
    email->set_primary(profileData.emailPrimary);
    email->set_reachable(profileData.emailReachable);
    email->set_type(profileData.emailType);
    email->set_updatedby(profileData.emailUpdatedBy);
    email->set_validated(profileData.emailValidated);
    email->set_validationcode(profileData.emailValidationCode);

    auto* emailValidatedAt = email->mutable_validatedat();
    emailValidatedAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(profileData.emailValidatedDate.time_since_epoch()).count());
    emailValidatedAt->set_nanos(0);

    // Set mii
    auto* mii = reply->mutable_mii();
    mii->set_id(profileData.miiId);
    mii->set_hash(profileData.miiHash);
    mii->set_name(profileData.miiName);
    mii->set_primary(profileData.miiPrimary);
    mii->set_data(profileData.miiData);

    // Get signed agreements
    auto agreementsCmd = db::Database::craftGetSignedAgreementsCommand(request->pid());
    const db::Result agreementsResult = co_await db->runCommand(std::move(agreementsCmd));

    if (agreementsResult.getStatus() == db::DBResultStatus::SUCCESS && agreementsResult.hasData()) {
        for (const auto& agreement : agreementsResult.getData<std::vector<db::DBUserAgreementData>>()) {
            auto* agr = reply->add_signedagreements();
            agr->set_type(agreement.type);
            agr->set_version(agreement.version);
            agr->set_country(agreement.country);
        }
    }

    // Get ownerships
    auto ownershipsCmd = db::Database::craftGetOwnershipsCommand(request->pid());
    const db::Result ownershipsResult = co_await db->runCommand(std::move(ownershipsCmd));

    if (ownershipsResult.getStatus() == db::DBResultStatus::SUCCESS && ownershipsResult.hasData()) {
        for (const auto& ownership : ownershipsResult.getData<std::vector<db::DBOwnershipData>>()) {
            auto* own = reply->add_owneddevices();

            // Get device details
            auto deviceCmd = db::Database::craftGetDeviceCommand(ownership.deviceId);
            const db::Result deviceResult = co_await db->runCommand(std::move(deviceCmd));

            if (deviceResult.getStatus() == db::DBResultStatus::SUCCESS && deviceResult.hasData()) {
                const auto deviceData = deviceResult.getData<db::DBDeviceData>();

                auto* device = own->mutable_device();
                device->set_id(deviceData.deviceId);
                device->set_language(deviceData.language);
                device->set_platform(dbPlatformToProto(deviceData.platformId));
                device->set_region(dbRegionToProto(deviceData.region));
                device->set_serialnumber(deviceData.serialNumber);
                device->set_systemversion(deviceData.systemVersion);
                device->set_type(deviceData.type);
                device->set_updatedby(deviceData.updatedBy);
                device->set_banned(deviceData.banned);
                device->set_status(deviceData.status);

                auto* timestamp = device->mutable_lastupdated();
                timestamp->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(deviceData.lastUpdated.time_since_epoch()).count());
                timestamp->set_nanos(0);
            }

            own->set_status(dbStatusToProto(ownership.status));

            auto* lastUpdated = own->mutable_lastupdated();
            lastUpdated->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(ownership.lastUpdated.time_since_epoch()).count());
            lastUpdated->set_nanos(0);

            // Get device attributes
            auto attrsCmd = db::Database::craftGetDeviceAttributesCommand(request->pid(), ownership.deviceId);
            const db::Result attrsResult = co_await db->runCommand(std::move(attrsCmd));

            if (attrsResult.getStatus() == db::DBResultStatus::SUCCESS && attrsResult.hasData()) {
                for (const auto& attr : attrsResult.getData<std::vector<db::DBDeviceAttributeData>>()) {
                    auto* attribute = own->add_accountdeviceattributes();
                    attribute->set_name(attr.name);
                    attribute->set_value(attr.value);

                    auto* createdAt = attribute->mutable_createdat();
                    createdAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(attr.createdDate.time_since_epoch()).count());
                    createdAt->set_nanos(0);
                }
            }
        }
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::UpdateAccount(grpc::CallbackServerContext* context,
    const AccountUpdate* request, Account* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] UpdateAccount called for PID: " +
               std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeUpdateAccount(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeDeleteAccount(grpc::ServerUnaryReactor* reactor, const AccountDeleteRequest* request,
                                const std::shared_ptr<db::Database> db,
                                const std::shared_ptr<SettingsManager> settingsManager,
                                const std::shared_ptr<Logger::Logger> logger) {
    // Create a database session for transaction support
    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start database transaction"));
        co_return;
    }

    // First get the user profile to retrieve miiId and emailId
    auto profileCmd = db::Database::craftGetUserProfileCommand(request->pid());
    auto profileResults = co_await session->runCommand(std::move(profileCmd));

    if (profileResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!profileResults.hasData()) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    auto userProfile = profileResults.getData<db::DBUserProfileData>();

    // Delete user ownerships
    auto deleteOwnershipsCmd = db::Database::craftDeleteUserOwnershipsCommand(request->pid());
    db::Result deleteOwnershipsResults = co_await session->runCommand(std::move(deleteOwnershipsCmd));
    if (deleteOwnershipsResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting ownerships"));
        co_return;
    }

    // Delete user agreements
    auto deleteUserAgreementsCmd = db::Database::craftDeleteUserAgreementsCommand(request->pid());
    db::Result deleteUserAgreementsResults = co_await session->runCommand(std::move(deleteUserAgreementsCmd));
    if (deleteUserAgreementsResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting agreements"));
        co_return;
    }

    // Delete user device attributes
    auto deleteUserDeviceAttributesCmd = db::Database::craftDeleteUserDeviceAttributesCommand(request->pid());
    db::Result deleteUserDeviceAttributesResults = co_await session->runCommand(std::move(deleteUserDeviceAttributesCmd));
    if (deleteUserDeviceAttributesResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting device attributes"));
        co_return;
    }

    // Delete the user
    auto deleteUserCmd = db::Database::craftDeleteUserCommand(request->pid());
    const db::Result deleteUserResults = co_await session->runCommand(std::move(deleteUserCmd));
    if (deleteUserResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting user"));
        co_return;
    }

    // Delete the mii
    auto deleteMiiCmd = db::Database::craftDeleteMiiCommand(userProfile.miiId);
    db::Result deleteMiiResults = co_await session->runCommand(std::move(deleteMiiCmd));
    if (deleteMiiResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting mii"));
        co_return;
    }

    // Delete the email
    auto deleteEmailCmd = db::Database::craftDeleteEmailCommand(userProfile.emailId);
    db::Result deleteEmailResults = co_await session->runCommand(std::move(deleteEmailCmd));
    if (deleteEmailResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting email"));
        co_return;
    }

    // Delete friends server account by setting up a task
    auto deleteFriendsServerAccountCmd = db::Database::craftInsertTaskCommand(
        static_cast<int>(util::GlobalTaskType::DELETE_FRIENDS_ACCOUNT), std::to_string(request->pid()));
    db::Result deleteFriendsServerAccountResults = co_await session->runCommand(std::move(deleteFriendsServerAccountCmd));
    if (deleteFriendsServerAccountResults.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error scheduling friends server account deletion"));
        co_return;
    }

    // Commit the transaction
    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to commit transaction"));
        co_return;
    }

    logger->log(Logger::level::INFO, Logger::group::GRPC,
        "Account with PID " + std::to_string(request->pid()) + " has been deleted successfully.");

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::DeleteAccount(grpc::CallbackServerContext* context,
    const AccountDeleteRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] DeleteAccount called for PID: " +
               std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeDeleteAccount(reactor, request, db, settingsManager, logger);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

// ==================== Email Management ====================

Task<void> completeUpdateAccountEmail(grpc::ServerUnaryReactor* reactor, const UpdateAccountEmailRequest* request,
                                     AccountEmail* reply, const std::shared_ptr<db::Database> db) {
    // First get the current email to merge values
    auto getEmailCmd = db::Database::craftGetUserEmailCommand(request->pid());
    const db::Result getResult = co_await db->runCommand(std::move(getEmailCmd));

    if (getResult.getStatus() != db::DBResultStatus::SUCCESS || !getResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Email not found"));
        co_return;
    }

    const auto currentEmail = getResult.getData<db::DBUserEmail>();
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    auto updateCmd = db::Database::craftInsertOrUpdateEmailCommand(
        request->emailid(),
        request->has_address() ? request->address() : currentEmail.email,
        request->has_parent() ? request->parent() : currentEmail.emailParent,
        request->has_primary() ? request->primary() : currentEmail.emailPrimary,
        request->has_reachable() ? request->reachable() : currentEmail.emailReachable,
        request->has_type() ? request->type() : currentEmail.emailType,
        request->has_updatedby() ? request->updatedby() : currentEmail.emailUpdatedBy,
        request->has_validated() ? request->validated() : currentEmail.emailValidated,
        request->has_validated() && request->validated() ? now : currentEmail.emailValidatedDate,
        request->has_validationcode() ? request->validationcode() : currentEmail.emailValidationCode
    );

    const db::Result result = co_await db->runCommand(std::move(updateCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Get updated email to return
    auto getUpdatedCmd = db::Database::craftGetUserEmailCommand(request->pid());
    const db::Result updatedResult = co_await db->runCommand(std::move(getUpdatedCmd));

    if (updatedResult.getStatus() == db::DBResultStatus::SUCCESS && updatedResult.hasData()) {
        const auto emailData = updatedResult.getData<db::DBUserEmail>();

        reply->set_id(emailData.emailId);
        reply->set_address(emailData.email);
        reply->set_parent(emailData.emailParent);
        reply->set_primary(emailData.emailPrimary);
        reply->set_reachable(emailData.emailReachable);
        reply->set_type(emailData.emailType);
        reply->set_updatedby(emailData.emailUpdatedBy);
        reply->set_validated(emailData.emailValidated);
        reply->set_validationcode(emailData.emailValidationCode);

        auto* validatedAt = reply->mutable_validatedat();
        validatedAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(emailData.emailValidatedDate.time_since_epoch()).count());
        validatedAt->set_nanos(0);
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::UpdateAccountEmail(grpc::CallbackServerContext* context,
    const UpdateAccountEmailRequest* request, AccountEmail* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] UpdateAccountEmail called for PID: " +
               std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeUpdateAccountEmail(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

// ==================== Mii Management ====================

Task<void> completeSetAccountMii(grpc::ServerUnaryReactor* reactor, const SetAccountMiiRequest* request,
                                AccountMii* reply, const std::shared_ptr<db::Database> db) {
    // Get user profile to find current mii ID
    auto getProfileCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result profileResult = co_await db->runCommand(std::move(getProfileCmd));

    if (profileResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!profileResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    std::optional<int64_t> miiId = std::nullopt;
    const auto profileData = profileResult.getData<db::DBUserProfileData>();
    miiId = profileData.miiId != 0 ? std::optional(profileData.miiId) : std::nullopt;

    // Create hash from mii data (random)
    std::string hash = crypto::genRandomString(13, "abcdefghijklmnopqrstuvwxyz0123456789");

    auto insertCmd = db::Database::craftInsertOrUpdateMiiCommand(
        miiId,
        hash,
        request->name(),
        request->primary(),
        request->data()
    );

    const db::Result result = co_await db->runCommand(std::move(insertCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Get the mii to return
    auto getMiiCmd = db::Database::craftGetUserMiiCommand(request->pid());
    const db::Result miiResult = co_await db->runCommand(std::move(getMiiCmd));

    if (miiResult.getStatus() == db::DBResultStatus::SUCCESS && miiResult.hasData()) {
        const auto miiData = miiResult.getData<db::DBUserMii>();

        reply->set_id(miiData.miiId);
        reply->set_hash(miiData.miiHash);
        reply->set_name(miiData.miiName);
        reply->set_primary(miiData.miiPrimary);
        reply->set_data(miiData.miiData);
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::SetAccountMii(grpc::CallbackServerContext* context,
    const SetAccountMiiRequest* request, AccountMii* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] SetAccountMii called for PID: " +
               std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeSetAccountMii(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

// ==================== Agreement Management ====================

Task<void> completeAddAccountAgreement(grpc::ServerUnaryReactor* reactor, const AddAccountAgreementRequest* request,
                                      const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Check if user exists
    auto checkCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result checkResult = co_await db->runCommand(std::move(checkCmd));

    if (checkResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    auto insertCmd = db::Database::craftInsertOrUpdateUserAgreementCommand(
        request->pid(),
        request->type(),
        static_cast<int>(request->version()),
        request->country(),
        now
    );

    const db::Result result = co_await db->runCommand(std::move(insertCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::AddAccountAgreement(grpc::CallbackServerContext* context,
    const AddAccountAgreementRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] AddAccountAgreement called for PID: " +
               std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeAddAccountAgreement(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeRemoveAccountAgreement(grpc::ServerUnaryReactor* reactor, const RemoveAccountAgreementRequest* request,
                                         const std::shared_ptr<db::Database> db) {
    // Check if user exists
    auto checkCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result checkResult = co_await db->runCommand(std::move(checkCmd));
    if (checkResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    auto deleteCmd = db::Database::craftDeleteUserAgreementCommand(
        request->pid(),
        request->type(),
        static_cast<int>(request->version()),
        request->country()
    );

    const db::Result result = co_await db->runCommand(std::move(deleteCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::RemoveAccountAgreement(grpc::CallbackServerContext* context,
    const RemoveAccountAgreementRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] RemoveAccountAgreement called for PID: " +
               std::to_string(request->pid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeRemoveAccountAgreement(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

// ==================== Device Ownership Management ====================

Task<void> completeLinkDeviceToAccount(grpc::ServerUnaryReactor* reactor, const LinkDeviceRequest* request,
                                      const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Check if user exists
    auto checkUserCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result checkResult = co_await db->runCommand(std::move(checkUserCmd));
    if (checkResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    // Check if device exists
    auto checkDeviceCmd = db::Database::craftGetDeviceCommand(request->deviceid());
    const db::Result checkDeviceResult = co_await db->runCommand(std::move(checkDeviceCmd));
    if (checkDeviceResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkDeviceResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Device not found"));
        co_return;
    }

    // Create a database session for transaction support
    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start database transaction"));
        co_return;
    }

    if (request->status() == STATUS_ACTIVE) {
        // Deactivate any other active devices for this user
        auto deactivateCmd = db::Database::craftInactivateAllUserOwnershipsCommand(request->pid());
        const db::Result deactivateResult = co_await session->runCommand(std::move(deactivateCmd));
        if (deactivateResult.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deactivating other devices"));
            co_return;
        }
    }

    // Insert ownership
    auto insertOwnershipCmd = db::Database::craftInsertOrUpdateOwnershipCommand(
        request->pid(),
        request->deviceid(),
        protoStatusToDb(request->status()),
        now
    );

    const db::Result result = co_await session->runCommand(std::move(insertOwnershipCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Insert device attributes
    for (const auto& attr : request->accountdeviceattributes()) {
        auto insertAttrCmd = db::Database::craftInsertOrUpdateDeviceAttributesCommand(
            request->deviceid(),
            request->pid(),
            attr.name(),
            attr.value(),
            now
        );

        const db::Result attrResult = co_await session->runCommand(std::move(insertAttrCmd));
        if (attrResult.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error inserting attributes"));
            co_return;
        }
    }

    // Commit the transaction
    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to commit transaction"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::LinkDeviceToAccount(grpc::CallbackServerContext* context,
    const LinkDeviceRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] LinkDeviceToAccount called for PID: " +
               std::to_string(request->pid()) + ", Device ID: " + std::to_string(request->deviceid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeLinkDeviceToAccount(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeUnlinkDeviceFromAccount(grpc::ServerUnaryReactor* reactor, const UnlinkDeviceRequest* request,
                                          const std::shared_ptr<db::Database> db) {
    // Create a database session for transaction support
    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start database transaction"));
        co_return;
    }

    // Check if ownership exists
    auto checkOwnershipCmd = db::Database::craftGetOwnershipCommand(request->pid(), request->deviceid());
    const db::Result checkResult = co_await session->runCommand(std::move(checkOwnershipCmd));
    if (checkResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkResult.hasData()) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Ownership not found"));
        co_return;
    }

    // Delete ownership
    auto deleteOwnershipCmd = db::Database::craftDeleteOwnershipCommand(request->pid(), request->deviceid());

    const db::Result result = co_await session->runCommand(std::move(deleteOwnershipCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Delete device attributes for this ownership
    auto deleteAttrsCmd = db::Database::craftDeleteDeviceAttributesForOwnershipCommand(request->pid(), request->deviceid());

    const db::Result attrsResult = co_await session->runCommand(std::move(deleteAttrsCmd));
    if (attrsResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error deleting attributes"));
        co_return;
    }

    // Commit the transaction
    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to commit transaction"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::UnlinkDeviceFromAccount(grpc::CallbackServerContext* context,
    const UnlinkDeviceRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] UnlinkDeviceFromAccount called for PID: " +
               std::to_string(request->pid()) + ", Device ID: " + std::to_string(request->deviceid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeUnlinkDeviceFromAccount(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeUpdateDeviceStatus(grpc::ServerUnaryReactor* reactor, const UpdateDeviceStatusRequest* request,
                                     const std::shared_ptr<db::Database> db) {
    // Create a database session for transaction support
    auto session = db->createSession();
    if ((co_await session->startTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start database transaction"));
        co_return;
    }

    // Check if ownership exists
    auto checkOwnershipCmd = db::Database::craftGetOwnershipCommand(request->pid(), request->deviceid());
    const db::Result checkResult = co_await session->runCommand(std::move(checkOwnershipCmd));
    if (checkResult.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkResult.hasData()) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Ownership not found"));
        co_return;
    }

    if (request->status() == STATUS_ACTIVE) {
        auto inactivateCmd = db::Database::craftInactivateAllUserOwnershipsCommand(request->pid());
        const db::Result inactivateResult = co_await session->runCommand(std::move(inactivateCmd));
        if (inactivateResult.getStatus() != db::DBResultStatus::SUCCESS) {
            co_await session->rollbackTransaction();
            reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
            co_return;
        }
    }

    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    auto updateCmd = db::Database::craftInsertOrUpdateOwnershipCommand(
        request->pid(),
        request->deviceid(),
        protoStatusToDb(request->status()),
        now
    );

    const db::Result result = co_await session->runCommand(std::move(updateCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        co_await session->rollbackTransaction();
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    // Commit the transaction
    if ((co_await session->commitTransaction()).getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to commit transaction"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::UpdateDeviceStatus(grpc::CallbackServerContext* context,
    const UpdateDeviceStatusRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] UpdateDeviceStatus called for PID: " +
               std::to_string(request->pid()) + ", Device ID: " + std::to_string(request->deviceid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeUpdateDeviceStatus(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

// ==================== Device Attribute Management ====================

Task<void> completeSetAccountDeviceAttribute(grpc::ServerUnaryReactor* reactor,
                                            const SetAccountDeviceAttributeRequest* request,
                                            const std::shared_ptr<db::Database> db) {
    const db::datetime_t now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());

    // Check if user exists
    auto checkCmd = db::Database::craftGetUserProfileCommand(request->pid());
    const db::Result checkResult = co_await db->runCommand(std::move(checkCmd));
    if (checkResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found"));
        co_return;
    }

    // Check if device exists
    auto checkDeviceCmd = db::Database::craftGetDeviceCommand(request->deviceid());
    const db::Result checkDeviceResult = co_await db->runCommand(std::move(checkDeviceCmd));
    if (checkDeviceResult.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (!checkDeviceResult.hasData()) {
        reactor->Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, "Device not found"));
        co_return;
    }

    auto insertCmd = db::Database::craftInsertOrUpdateDeviceAttributesCommand(
        request->deviceid(),
        request->pid(),
        request->attributename(),
        request->attributevalue(),
        now
    );

    const db::Result result = co_await db->runCommand(std::move(insertCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::SetAccountDeviceAttribute(grpc::CallbackServerContext* context,
    const SetAccountDeviceAttributeRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] SetAccountDeviceAttribute called for PID: " +
               std::to_string(request->pid()) + ", Device ID: " + std::to_string(request->deviceid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeSetAccountDeviceAttribute(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeRemoveAccountDeviceAttribute(grpc::ServerUnaryReactor* reactor,
                                                const RemoveAccountDeviceAttributeRequest* request,
                                                const std::shared_ptr<db::Database> db) {
    auto deleteCmd = db::Database::craftDeleteUserDeviceAttributeCommand(
        request->pid(),
        request->deviceid(),
        request->attributename()
    );

    const db::Result result = co_await db->runCommand(std::move(deleteCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::RemoveAccountDeviceAttribute(grpc::CallbackServerContext* context,
    const RemoveAccountDeviceAttributeRequest* request, google::protobuf::Empty* _) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] RemoveAccountDeviceAttribute called for PID: " +
               std::to_string(request->pid()) + ", Device ID: " + std::to_string(request->deviceid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeRemoveAccountDeviceAttribute(reactor, request, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

Task<void> completeListAccountDeviceAttributes(grpc::ServerUnaryReactor* reactor,
                                               const ListAccountDeviceAttributesRequest* request,
                                               ListAccountDeviceAttributesResponse* reply,
                                               const std::shared_ptr<db::Database> db) {
    auto getCmd = db::Database::craftGetDeviceAttributesCommand(request->pid(), request->deviceid());

    const db::Result result = co_await db->runCommand(std::move(getCmd));
    if (result.getStatus() != db::DBResultStatus::SUCCESS) {
        reactor->Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Database error"));
        co_return;
    }

    if (result.hasData()) {
        for (const auto& attr : result.getData<std::vector<db::DBDeviceAttributeData>>()) {
            auto* attribute = reply->add_attributes();
            attribute->set_name(attr.name);
            attribute->set_value(attr.value);

            auto* createdAt = attribute->mutable_createdat();
            createdAt->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(attr.createdDate.time_since_epoch()).count());
            createdAt->set_nanos(0);
        }
    }

    reactor->Finish(grpc::Status::OK);
}

grpc::ServerUnaryReactor* AccountManagementServiceImpl::ListAccountDeviceAttributes(grpc::CallbackServerContext* context,
    const ListAccountDeviceAttributesRequest* request, ListAccountDeviceAttributesResponse* reply) {

    logger->log(Logger::level::DEBUG, Logger::group::GRPC,
               "[" + std::string(AccountManagementService::service_full_name()) + "] ListAccountDeviceAttributes called for PID: " +
               std::to_string(request->pid()) + ", Device ID: " + std::to_string(request->deviceid()));

    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();

    auto task = completeListAccountDeviceAttributes(reactor, request, reply, db);
    task.setContext(reactor);
    httpServer->scheduleArbitraryFunction(std::move(task));

    return reactor;
}

} // namespace grpcimpl::accountmanagement::v1