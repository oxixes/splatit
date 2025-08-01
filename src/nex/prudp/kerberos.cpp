#include "kerberos.hpp"
#include "../../crypto/tools.hpp"
#include "../../crypto/arc4.hpp"
#include "../types/common/datetime.hpp"
#include "../types/common/ints.hpp"
#include "../types/common/buffer.hpp"

namespace nex::prudp::kerberos {

std::vector<uint8_t> generateUserKey(const std::vector<uint8_t>& password, uint32_t pid) {
    std::vector<uint8_t> key = password;
    for (uint32_t i = 0; i < 65000 + (pid % 1024); i++) {
        key = crypto::MD5(key);
    }

    return key;
}

void decryptConnectRequest(const std::vector<uint8_t>& ticket, const std::vector<uint8_t>& securePasswd, uint32_t securePid,
                           std::vector<uint8_t>& outSessionKey, uint32_t& outPid, uint32_t& outConnectionId,
                           uint32_t& outResponseCheckValue) {
    if (ticket.size() < 4) throw std::runtime_error("Ticket is too short");

    uint32_t internalTicketLength = ticket[0] | ticket[1] << 8 | ticket[2] << 16 | ticket[3] << 24;
    if (ticket.size() < internalTicketLength + 8) throw std::runtime_error("Internal ticket length is invalid");

    uint32_t requestDataLength = ticket[internalTicketLength + 4] | ticket[internalTicketLength + 5] << 8 |
                                 ticket[internalTicketLength + 6] << 16 | ticket[internalTicketLength + 7] << 24;
    if (ticket.size() < internalTicketLength + requestDataLength + 8) throw std::runtime_error("Request data length is invalid");

    std::vector<uint8_t> ticketData(ticket.begin() + 4, ticket.begin() + 4 + internalTicketLength);
    std::vector<uint8_t> requestData(ticket.begin() + 8 + internalTicketLength,
                                     ticket.begin() + 8 + internalTicketLength + requestDataLength);

    std::vector<uint8_t> targetKey = generateUserKey(securePasswd, securePid);

    // We process the ticket data first
    if (ticketData.size() < 16) throw std::runtime_error("Ticket data is too short");
    std::vector<uint8_t> ticketDataNoMac(ticketData.begin(), ticketData.begin() + (std::ptrdiff_t) ticketData.size() - 16);
    std::vector<uint8_t> mac = crypto::HMAC_MD5(targetKey, ticketDataNoMac);
    std::vector<uint8_t> ticketMac(ticketData.begin() + (std::ptrdiff_t) ticketData.size() - 16, ticketData.end());
    if (mac != ticketMac) throw std::runtime_error("Ticket MAC is invalid");

    ARC4 arc4(targetKey);
    std::vector<uint8_t> decryptedTicketData = arc4.decrypt(ticketDataNoMac);

    if (decryptedTicketData.size() < 12) throw std::runtime_error("Ticket data is too short");

    auto expireDate = std::chrono::system_clock::now();
    expireDate += std::chrono::minutes(2);
    rmc::Datetime ticketDate;
    ticketDate.decode(decryptedTicketData);
    if (ticketDate > expireDate) throw std::runtime_error("Ticket is expired");

    outPid = decryptedTicketData[8] | decryptedTicketData[9] << 8 | decryptedTicketData[10] << 16 |
             decryptedTicketData[11] << 24;

    outSessionKey = std::vector<uint8_t>(decryptedTicketData.begin() + 12, decryptedTicketData.end());

    // Now we process the request data
    if (requestData.size() < 16) throw std::runtime_error("Request data is too short");
    std::vector<uint8_t> requestDataNoMac(requestData.begin(), requestData.begin() + (std::ptrdiff_t) requestData.size() - 16);
    mac = crypto::HMAC_MD5(outSessionKey, requestDataNoMac);
    std::vector<uint8_t> requestMac(requestData.begin() + (std::ptrdiff_t) requestData.size() - 16, requestData.end());
    if (mac != requestMac) throw std::runtime_error("Request MAC is invalid");

    arc4 = ARC4(outSessionKey);
    std::vector<uint8_t> decryptedRequestData = arc4.decrypt(requestDataNoMac);

    if (decryptedRequestData.size() < 12) throw std::runtime_error("Request data is too short");
    uint32_t userPid = decryptedRequestData[0] | decryptedRequestData[1] << 8 | decryptedRequestData[2] << 16 |
                       decryptedRequestData[3] << 24;

    if (userPid != outPid) throw std::runtime_error("User PID doesn't match ticket PID");

    outConnectionId = decryptedRequestData[4] | decryptedRequestData[5] << 8 | decryptedRequestData[6] << 16 |
                      decryptedRequestData[7] << 24;

    outResponseCheckValue = decryptedRequestData[8] | decryptedRequestData[9] << 8 | decryptedRequestData[10] << 16 |
                            decryptedRequestData[11] << 24;
}

std::vector<uint8_t> generateTicket(uint32_t user_pid, const std::vector<uint8_t>& user_passwd, uint32_t target_pid,
                                    const std::vector<uint8_t>& target_passwd, bool friends = false) {
    std::vector<uint8_t> sessionKey = crypto::genKey((friends) ? 16 : 32);

    auto currentDatetime = rmc::Datetime(std::chrono::system_clock::now());
    auto sourcePid = rmc::PID(0, user_pid);
    auto targetPid = rmc::PID(0, target_pid);

    auto sourceKey = generateUserKey(user_passwd, user_pid);
    auto targetKey = generateUserKey(target_passwd, target_pid);

    std::vector<uint8_t> internalUnencypted;
    internalUnencypted.reserve(12 + sessionKey.size());

    auto encodedDatetime = currentDatetime.encode();
    internalUnencypted.insert(internalUnencypted.end(), encodedDatetime.begin(), encodedDatetime.end());

    auto encodedSourcePid = sourcePid.encode();
    internalUnencypted.insert(internalUnencypted.end(), encodedSourcePid.begin(), encodedSourcePid.end());

    internalUnencypted.insert(internalUnencypted.end(), sessionKey.begin(), sessionKey.end());

    ARC4 arc4(targetKey);
    std::vector<uint8_t> internalTicket = arc4.encrypt(internalUnencypted);

    auto mac = crypto::HMAC_MD5(targetKey, internalTicket);
    internalTicket.insert(internalTicket.end(), mac.begin(), mac.end());

    std::vector<uint8_t> ticketUnencrypted;
    ticketUnencrypted.reserve(8 + internalTicket.size() + sessionKey.size());

    ticketUnencrypted.insert(ticketUnencrypted.end(), sessionKey.begin(), sessionKey.end());

    auto encodedTargetPid = targetPid.encode();
    ticketUnencrypted.insert(ticketUnencrypted.end(), encodedTargetPid.begin(), encodedTargetPid.end());

    rmc::Buffer internalTicketBuffer(0, internalTicket);
    auto encodedInternalTicket = internalTicketBuffer.encode();
    ticketUnencrypted.insert(ticketUnencrypted.end(), encodedInternalTicket.begin(), encodedInternalTicket.end());

    arc4 = ARC4(sourceKey);
    auto ticket = arc4.encrypt(ticketUnencrypted);

    mac = crypto::HMAC_MD5(sourceKey, ticket);
    ticket.insert(ticket.end(), mac.begin(), mac.end());

    return ticket;
}

} // namespace nex::prudp::kerberos