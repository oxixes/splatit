#ifndef SPLATOON_SERVER_GRCPIMPL_COMMON_HPP
#define SPLATOON_SERVER_GRCPIMPL_COMMON_HPP

#include <common.pb.h>

#include "../../nex/friends/friendsSecure.hpp"

namespace grpcimpl::common {

nex::rmc::ClientInfo deserializeClientInfo(const ClientInfo* clientInfo);
void serializeClientInfo(const nex::rmc::ClientInfo& clientInfo, ClientInfo* serializedClientInfo);

} // namespace grcpimpl::common

#endif //SPLATOON_SERVER_GRCPIMPL_COMMON_HPP