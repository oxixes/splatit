#include "common.hpp"

namespace grpcimpl::common {

nex::rmc::ClientInfo deserializeClientInfo(const ClientInfo* clientInfo) {
    const sock::IPv4Addr ipv4Addr {
        .a = static_cast<uint8_t>(clientInfo->address().address().a()),
        .b = static_cast<uint8_t>(clientInfo->address().address().b()),
        .c = static_cast<uint8_t>(clientInfo->address().address().c()),
        .d = static_cast<uint8_t>(clientInfo->address().address().d()),
        .port = static_cast<uint16_t>(clientInfo->address().address().port())
    };

    const nex::prudp::PRUDPAddress prudpAddress {
        .address = ipv4Addr,
        .vPort = static_cast<uint8_t>(clientInfo->address().vport()),
        .streamType = static_cast<uint8_t>(clientInfo->address().streamtype()),
        .srcVPort = static_cast<uint8_t>(clientInfo->address().srcvport()),
        .srcStreamType = static_cast<uint8_t>(clientInfo->address().srcstreamtype())
    };

    const nex::rmc::ClientInfo result {
        .address = prudpAddress,
        .minorVersion = static_cast<uint8_t>(clientInfo->minorversion()),
        .substreamId = static_cast<uint8_t>(clientInfo->substreamid()),
        .serverId = clientInfo->serverid(),
        .pid = clientInfo->pid()
    };

    return result;
}

void serializeClientInfo(const nex::rmc::ClientInfo& clientInfo, ClientInfo* serializedClientInfo) {
    serializedClientInfo->mutable_address()->mutable_address()->set_a(clientInfo.address.address.a);
    serializedClientInfo->mutable_address()->mutable_address()->set_b(clientInfo.address.address.b);
    serializedClientInfo->mutable_address()->mutable_address()->set_c(clientInfo.address.address.c);
    serializedClientInfo->mutable_address()->mutable_address()->set_d(clientInfo.address.address.d);
    serializedClientInfo->mutable_address()->mutable_address()->set_port(clientInfo.address.address.port);
    serializedClientInfo->mutable_address()->set_vport(clientInfo.address.vPort);
    serializedClientInfo->mutable_address()->set_streamtype(clientInfo.address.streamType);
    serializedClientInfo->mutable_address()->set_srcvport(clientInfo.address.srcVPort);
    serializedClientInfo->mutable_address()->set_srcstreamtype(clientInfo.address.srcStreamType);
    serializedClientInfo->set_minorversion(clientInfo.minorVersion);
    serializedClientInfo->set_substreamid(clientInfo.substreamId);
    serializedClientInfo->set_serverid(clientInfo.serverId);
    serializedClientInfo->set_pid(clientInfo.pid);
}

} // namespace grpcimpl::common