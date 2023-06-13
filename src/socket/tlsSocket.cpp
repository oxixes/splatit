#include <stdexcept>
#include "tlsSocket.hpp"

// TODO Handle non blocking accept
// TODO Improve error handling by using SSL_get_error

namespace sock {

TLSSocket::TLSSocket(bool server, EVP_PKEY* key, X509* cert) : TCPSocket() {
    const SSL_METHOD* method = server ? TLS_server_method() : TLS_client_method();
    ctx = SSL_CTX_new(method);

    if (!ctx) {
        throw FatalException("Failed to create SSL context: " + util::getOpenSSLError());
    }

    if (key && cert) {
        SSL_CTX_use_certificate(ctx, cert);
        SSL_CTX_use_PrivateKey(ctx, key);
    }
}

#ifdef _WIN32
TLSSocket::TLSSocket(SOCKET socket, SSL_CTX* ctx, SSL* ssl) : TCPSocket(socket), ctx(ctx), ssl(ssl) {}
#else
TLSSocket::TLSSocket(int socket, SSL_CTX* ctx, SSL* ssl) : TCPSocket(socket), ctx(ctx), ssl(ssl) {}
#endif

TLSSocket::~TLSSocket() {
    if (ssl) {
        SSL_free(ssl);
        ssl = nullptr;
    }

    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
}

TLSSocket* TLSSocket::accept(struct sockaddr* addr, socklen_t* addrlen) const {
    SSL* sslPtr = SSL_new(ctx);

    if (!sslPtr) {
        throw FatalException("Failed to create SSL object: " + util::getOpenSSLError());
    }

    auto* newSocket = new TLSSocket(acceptAux(addr, addrlen), ctx, sslPtr);
    SSL_CTX_up_ref(ctx);

    if (!SSL_set_fd(newSocket->ssl, (int) newSocket->socket)) {
        throw FatalException("Failed to set SSL file descriptor: " + util::getOpenSSLError());
    }

    int ret = SSL_accept(newSocket->ssl);
    if (ret <= 0) {
        throw FatalException("Failed to accept TLS connection: " + util::getOpenSSLError());
    }

    return newSocket;
}

void TLSSocket::connect(const struct sockaddr* addr, socklen_t addrlen) {
    ssl = SSL_new(ctx);

    if (!ssl) {
        throw FatalException("Failed to create SSL object: " + util::getOpenSSLError());
    }

    if (!SSL_set_fd(ssl, (int) socket)) {
        throw FatalException("Failed to set SSL file descriptor: " + util::getOpenSSLError());
    }

    TCPSocket::connect(addr, addrlen);

    if (SSL_connect(ssl) <= 0) {
        throw FatalException("Failed to connect TLS connection: " + util::getOpenSSLError());
    }
}

int TLSSocket::send(const void* buf, size_t len, int flags) const {
    size_t bytesWritten = 0;
    int result = SSL_write_ex(ssl, buf, len, &bytesWritten);

    if (result < 0) {
        unsigned long error = SSL_get_error(ssl, result);
        if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ) {
            throw RetryableException("Failed to send data: " + util::getOpenSSLError(&error));
        } else {
            throw FatalException("Failed to send data: " + util::getOpenSSLError(&error));
        }
    }

    return (int) bytesWritten;
}

int TLSSocket::recv(void* buf, size_t len, int flags) const {
    size_t bytesRead = 0;
    int result = SSL_read_ex(ssl, buf, len, &bytesRead);

    if (result < 0) {
        unsigned long error = SSL_get_error(ssl, result);
        if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ) {
            throw RetryableException("Failed to receive data: " + util::getOpenSSLError(&error));
        } else {
            throw FatalException("Failed to receive data: " + util::getOpenSSLError(&error));
        }
    }

    return (int) bytesRead;
}

void TLSSocket::shutdown() const {
    SSL_shutdown(ssl);
    TCPSocket::shutdown();
}

void TLSSocket::close() {
    if (ssl) {
        SSL_free(ssl);
        ssl = nullptr;
    }

    TCPSocket::close();
}

} // namespace sock::tcp