#ifndef SPLATOON_SERVER_KERBEROS_HPP
#define SPLATOON_SERVER_KERBEROS_HPP

#include <stdexcept>
#include <cstdint>
#include <vector>

namespace nex::prudp::kerberos {

std::vector<uint8_t> generateUserKey(const std::vector<uint8_t>& password, uint32_t pid);
void decryptConnectRequest(const std::vector<uint8_t>& ticket, const std::vector<uint8_t>& securePasswd, uint32_t securePid,
                           std::vector<uint8_t>& outSessionKey, uint32_t& outPid, uint32_t& outConnectionId,
                           uint32_t& outResponseCheckValue);

} // namespace nex::prudp::kerberos

#endif //SPLATOON_SERVER_KERBEROS_HPP
