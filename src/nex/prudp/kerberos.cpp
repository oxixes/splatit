#include "kerberos.hpp"
#include "../../crypto/tools.hpp"
#include "../../crypto/arc4.hpp"

namespace prudp::kerberos {

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
    if (ticket.size() < internalTicketLength + 8) throw std::runtime_error("Ticket is too short");

    uint32_t requestDataLength = ticket[internalTicketLength + 4] | ticket[internalTicketLength + 5] << 8 |
                                 ticket[internalTicketLength + 6] << 16 | ticket[internalTicketLength + 7] << 24;
    if (ticket.size() < internalTicketLength + requestDataLength + 8) throw std::runtime_error("Ticket is too short");

    std::vector<uint8_t> ticketData(ticket.begin() + 4, ticket.begin() + 4 + internalTicketLength);
    std::vector<uint8_t> requestData(ticket.begin() + 8 + internalTicketLength,
                                     ticket.begin() + 8 + internalTicketLength + requestDataLength);

    std::vector<uint8_t> targetKey = generateUserKey(securePasswd, securePid);

    // We process the ticket data first
    if (ticketData.size() < 16) throw std::runtime_error("Ticket data is too short");
    std::vector<uint8_t> ticketDataNoMac(ticketData.begin(), ticketData.begin() + (ssize_t) ticketData.size() - 16);
    std::vector<uint8_t> mac = crypto::HMAC_MD5(targetKey, ticketDataNoMac);
    std::vector<uint8_t> ticketMac(ticketData.begin() + (ssize_t) ticketData.size() - 16, ticketData.end());
    if (mac != ticketMac) throw std::runtime_error("Ticket MAC is invalid");

    ARC4 arc4(targetKey);
    std::vector<uint8_t> decryptedTicketData = arc4.decrypt(ticketDataNoMac);

    if (decryptedTicketData.size() < 12) throw std::runtime_error("Ticket data is too short");

    // TODO Check ticket date to see if it's expired

    outPid = decryptedTicketData[8] | decryptedTicketData[9] << 8 | decryptedTicketData[10] << 16 |
             decryptedTicketData[11] << 24;

    outSessionKey = std::vector<uint8_t>(decryptedTicketData.begin() + 12, decryptedTicketData.end());

    // Now we process the request data
    if (requestData.size() < 16) throw std::runtime_error("Request data is too short");
    std::vector<uint8_t> requestDataNoMac(requestData.begin(), requestData.begin() + (ssize_t) requestData.size() - 16);
    mac = crypto::HMAC_MD5(outSessionKey, requestDataNoMac);
    std::vector<uint8_t> requestMac(requestData.begin() + (ssize_t) requestData.size() - 16, requestData.end());
    if (mac != requestMac) throw std::runtime_error("Request MAC is invalid");

    arc4 = ARC4(outSessionKey);
    std::vector<uint8_t> decryptedRequestData = arc4.decrypt(requestDataNoMac);

    if (decryptedRequestData.size() < 12) throw std::runtime_error("Request data is too short");
    uint32_t userPid = decryptedRequestData[8] | decryptedRequestData[9] << 8 | decryptedRequestData[10] << 16 |
                       decryptedRequestData[11] << 24;

    if (userPid != outPid) throw std::runtime_error("User PID doesn't match ticket PID");

    outConnectionId = decryptedRequestData[0] | decryptedRequestData[1] << 8 | decryptedRequestData[2] << 16 |
                      decryptedRequestData[3] << 24;

    outResponseCheckValue = decryptedRequestData[4] | decryptedRequestData[5] << 8 | decryptedRequestData[6] << 16 |
                            decryptedRequestData[7] << 24;
}

} // namespace prudp::kerberos