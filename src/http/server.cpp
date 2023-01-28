#include "server.hpp"

HTTP_Server::HTTP_Server() {
    server = new httplib::SSLServer("certs/any.nintendo.net.crt", "cert/any.nintendo.net.key");
}

HTTP_Server::~HTTP_Server() {
    server->stop();
    delete server;
}

void HTTP_Server::listen() {

}

void HTTP_Server::stop() {

}