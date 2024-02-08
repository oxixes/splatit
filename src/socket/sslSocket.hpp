#ifndef SPLATOON_SERVER_SSLSOCKET_HPP
#define SPLATOON_SERVER_SSLSOCKET_HPP

#include "socket.hpp"
#include "../util/util.hpp"

#include <openssl/ssl.h>

namespace sock {

class SSLSocket : public TCPSocket {
public:
    explicit SSLSocket(bool server = true, EVP_PKEY* key = nullptr, X509* cert = nullptr);
    ~SSLSocket() override;

    SSLSocket* accept(struct sockaddr* addr, socklen_t* addrlen) override;
    void connect(const struct sockaddr* addr, socklen_t addrlen) override;
    void connect() override;
    int send(const void* buf, size_t len, int flags) override;
    int recv(void* buf, size_t len, int flags) override;
    void close(bool force) override;

private:
#ifdef _WIN32
    explicit SSLSocket(SOCKET socket, SSL_CTX* ctx, SSL* ssl);
#else
    explicit SSLSocket(int socket, SSL_CTX* ctx, SSL* ssl);
#endif

    SSL_CTX* ctx;
    SSL* ssl = nullptr;

    bool server = false;
    bool fatalErrorOcurred = false;
};

} // namespace sock::tcp

#endif //SPLATOON_SERVER_SSLSOCKET_HPP
