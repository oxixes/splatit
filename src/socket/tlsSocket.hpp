#ifndef SPLATOON_SERVER_TLSSOCKET_HPP
#define SPLATOON_SERVER_TLSSOCKET_HPP

#include "socket.hpp"
#include "../util/util.hpp"

#include <openssl/ssl.h>

namespace sock {

class TLSSocket : public TCPSocket {
public:
    explicit TLSSocket(bool server = true, EVP_PKEY* key = nullptr, X509* cert = nullptr);
    ~TLSSocket() override;

    TLSSocket* accept(struct sockaddr* addr, socklen_t* addrlen) const override;
    void connect(const struct sockaddr* addr, socklen_t addrlen) override;
    int send(const void* buf, size_t len, int flags) const override;
    int recv(void* buf, size_t len, int flags) const override;
    void close() override;
    void shutdown() const override;

private:
#ifdef _WIN32
    explicit TLSSocket(SOCKET socket, SSL_CTX* ctx, SSL* ssl);
#else
    explicit TLSSocket(int socket, SSL_CTX* ctx, SSL* ssl);
#endif

    SSL_CTX* ctx;
    SSL* ssl = nullptr;
};

} // namespace sock::tcp

#endif //SPLATOON_SERVER_TLSSOCKET_HPP
