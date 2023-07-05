#include <stdexcept>
#include "sslSocket.hpp"

// TODO Allow for client / server certificates checking

namespace sock {

SSLSocket::SSLSocket(bool server, EVP_PKEY* key, X509* cert) : TCPSocket() {
    this->server = server;
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
SSLSocket::SSLSocket(SOCKET socket, SSL_CTX* ctx, SSL* ssl) : TCPSocket(socket), ctx(ctx), ssl(ssl) {}
#else
TLSSocket::TLSSocket(int socket, SSL_CTX* ctx, SSL* ssl) : TCPSocket(socket), ctx(ctx), ssl(ssl) {}
#endif

SSLSocket::~SSLSocket() {
    if (ssl) {
        SSL_free(ssl);
        ssl = nullptr;
    }

    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
}

SSLSocket* SSLSocket::accept(struct sockaddr* addr, socklen_t* addrlen) {
    if (status != SocketStatus::LISTENING) {
        throw FatalException("Not listening, cannot continue (tried accepting)");
    }

    SSL* sslPtr = SSL_new(ctx);

    if (!sslPtr) {
        throw FatalException("Failed to create SSL object: " + util::getOpenSSLError());
    }

    auto* newSocket = new SSLSocket(acceptAux(addr, addrlen), ctx, sslPtr);
    SSL_CTX_up_ref(ctx);

    newSocket->server = true;

    if (!SSL_set_fd(newSocket->ssl, (int) newSocket->socket)) {
        delete newSocket;
        throw SSLException("Failed to set SSL file descriptor: " + util::getOpenSSLError());
    }

    int ret = SSL_accept(newSocket->ssl);
    if (ret <= 0) {
        unsigned long error = SSL_get_error(newSocket->ssl, ret);
        if (error == SSL_ERROR_WANT_WRITE) {
            newSocket->lastResult = ResultType::NEEDS_WRITE;
            newSocket->status = SocketStatus::CONNECTING;
//            throw RetryableException("Failed to send data: " + util::getOpenSSLError(&error));
        } else if (error == SSL_ERROR_WANT_READ) {
            newSocket->lastResult = ResultType::NEEDS_READ;
            newSocket->status = SocketStatus::CONNECTING;
//            throw RetryableException("Failed to send data: " + util::getOpenSSLError(&error));
        } else {
//            newSocket->status = SocketStatus::FAILURE;
            delete newSocket;
            throw SSLException("Failed to accept TLS connection: " + std::to_string(error));
        }
    } else {
        newSocket->lastResult = ResultType::SUCCESS;
        newSocket->status = SocketStatus::CONNECTED;
    }

    return newSocket;
}

void SSLSocket::connect(const struct sockaddr* addr, socklen_t addrlen) {
    if (status != SocketStatus::NOT_CONNECTED) {
        throw FatalException("Not disconnected, cannot continue (tried connecting)");
    }
    if (status == SocketStatus::CONNECTED) {
        lastResult = ResultType::SUCCESS;
        return;
    }

    ssl = SSL_new(ctx);

    if (!ssl) {
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to create SSL object: " + util::getOpenSSLError());
    }

    if (!SSL_set_fd(ssl, (int) socket)) {
        status = SocketStatus::FAILURE;
        throw FatalException("Failed to set SSL file descriptor: " + util::getOpenSSLError());
    }

    TCPSocket::connect(addr, addrlen);

    status = SocketStatus::CONNECTING;
    connect();

    status = SocketStatus::CONNECTED;
    lastResult = ResultType::SUCCESS;
}

void SSLSocket::connect() {
    if (status != SocketStatus::CONNECTING) {
        throw FatalException("Not connecting, cannot continue (tried connecting)");
    }
    if (status == SocketStatus::CONNECTED) {
        lastResult = ResultType::SUCCESS;
        return;
    }

    int result;
    if (server) {
        result = SSL_accept(ssl);
    } else {
        result = SSL_connect(ssl);
    }

    if (result <= 0) {
        unsigned long error = SSL_get_error(ssl, result);
        if (error == SSL_ERROR_WANT_WRITE) {
            lastResult = ResultType::NEEDS_WRITE;
            status = SocketStatus::CONNECTING;
            throw RetryableException("Failed to send data: " + std::to_string(error));
        } else if (error == SSL_ERROR_WANT_READ) {
            lastResult = ResultType::NEEDS_READ;
            status = SocketStatus::CONNECTING;
            throw RetryableException("Failed to send data: " + std::to_string(error));
        } else {
            if (error == SSL_ERROR_SYSCALL || error == SSL_ERROR_SSL) fatalErrorOcurred = true;
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to connect TLS connection: " + std::to_string(error));
        }
    } else {
        status = SocketStatus::CONNECTED;
        lastResult = ResultType::SUCCESS;
    }
}

int SSLSocket::send(const void* buf, size_t len, int flags) {
    if (status != SocketStatus::CONNECTED) {
        throw FatalException("Not connected, cannot continue (tried sending)");
    }

    size_t bytesWritten = 0;
    int result = SSL_write_ex(ssl, buf, len, &bytesWritten);

    if (result <= 0) {
        unsigned long error = SSL_get_error(ssl, result);
        if (error == SSL_ERROR_WANT_WRITE) {
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to send data: " + std::to_string(error));
        } else if (error == SSL_ERROR_WANT_READ) {
            lastResult = ResultType::NEEDS_READ;
            throw RetryableException("Failed to send data: " + std::to_string(error));
        } else {
            if (error == SSL_ERROR_SYSCALL || error == SSL_ERROR_SSL) fatalErrorOcurred = true;
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to send data: " + std::to_string(error));
        }
    }

    lastResult = ResultType::SUCCESS;

    return (int) bytesWritten;
}

int SSLSocket::recv(void* buf, size_t len, int flags) {
    if (status != SocketStatus::CONNECTED) {
        throw FatalException("Not connected, cannot continue (tried reading)");
    }

    size_t bytesRead = 0;
    int result = SSL_read_ex(ssl, buf, len, &bytesRead);

    if (result <= 0) {
        unsigned long error = SSL_get_error(ssl, result);
        if (error == SSL_ERROR_WANT_WRITE) {
            lastResult = ResultType::NEEDS_WRITE;
            throw RetryableException("Failed to receive data: " + std::to_string(error));
        } else if (error == SSL_ERROR_WANT_READ) {
            lastResult = ResultType::NEEDS_READ;
            throw RetryableException("Failed to receive data: " + std::to_string(error));
        } else if (error == SSL_ERROR_ZERO_RETURN) {
            bytesRead = 0;
            // Just in case
            ERR_clear_error();
        } else {
            if (error == SSL_ERROR_SYSCALL || error == SSL_ERROR_SSL) fatalErrorOcurred = true;
            status = SocketStatus::FAILURE;
            throw FatalException("Failed to receive data: " + std::to_string(error));
        }
    }

    lastResult = ResultType::SUCCESS;

    return (int) bytesRead;
}

void SSLSocket::close(bool force) {
    if (!fatalErrorOcurred && !force && ssl != nullptr) {
        int result = SSL_shutdown(ssl);
        if (result <= 0) {
            unsigned long error = SSL_get_error(ssl, result);
            if (error == SSL_ERROR_WANT_WRITE) {
                lastResult = ResultType::NEEDS_WRITE;
                status = SocketStatus::CLOSING;
                throw RetryableException("Failed to close: " + std::to_string(error));
            } else if (error == SSL_ERROR_WANT_READ) {
                lastResult = ResultType::NEEDS_READ;
                status = SocketStatus::CLOSING;
                throw RetryableException("Failed to close: " + std::to_string(error));
            }
        }
    } else if (!fatalErrorOcurred && ssl != nullptr) {
        SSL_shutdown(ssl); // Since we're force closing the connection, we don't care about the result,
                              // but it's still better to try and close the connection gracefully (by sending
                              // a close alert).
    }

    if (ssl) {
        SSL_free(ssl);
        ssl = nullptr;
    }

    TCPSocket::close(force);
}

} // namespace sock::tcp