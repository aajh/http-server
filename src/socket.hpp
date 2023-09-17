#pragma once

#include "common.hpp"
#include <tl/expected.hpp>

struct Connection {
    int fd;

    Connection(int connection_fd) : fd(connection_fd) {}
    Connection(const Connection&) = delete;
    Connection(Connection&& o) : fd(o.fd) {
        o.fd = -1;
    };
    ~Connection();

    tl::expected<size_t, const char*> send_raw(const char* data, size_t length);
    const char* send(const char* data, size_t length);
};

struct Socket {
    int fd;

    Socket(int socket_fd) : fd(socket_fd) {}
    Socket(const Socket&) = delete;
    Socket(Socket&& o) : fd(o.fd) {
        o.fd = -1;
    };
    ~Socket();

    static tl::expected<Socket, const char*> bind(const char* port);
    static tl::expected<Socket, const char*> bind_and_listen(const char* port, const int backlog) {
        auto socket = bind(port);
        if (!socket) {
            return socket;
        }

        if (auto error = socket->listen(backlog)) {
            return tl::unexpected(error);
        }

        return socket;
    }

    const char* listen(const int backlog);
    tl::expected<Connection, const char*> accept();
};
