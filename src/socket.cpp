#include "socket.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

Connection::~Connection() {
    if (fd != -1) {
        close(fd);
    }
}

tl::expected<Socket, const char*> Socket::bind(const char* port) {
    int status = -1, socket_fd = -1;

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* server_info;
    status = getaddrinfo(nullptr, port, &hints, &server_info);
    if (status) {
        return tl::unexpected(gai_strerror(status));
    }
    defer { freeaddrinfo(server_info); };

    for (addrinfo* a = server_info; a != nullptr; a = a->ai_next) {
        socket_fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (socket_fd == -1) {
            continue;
        }

        status = ::bind(socket_fd, a->ai_addr, a->ai_addrlen);
        if (status) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        break;
    }

    if (socket_fd == -1) {
        return tl::unexpected(server_info ? strerror(errno) : "getaddrinfo did not return values");
    }

    return Socket(socket_fd);
}

tl::expected<size_t, const char*> Connection::send(void *data, size_t length) {
    ssize_t bytes_sent = ::send(fd, data, length, 0);
    if (bytes_sent == -1) {
        return tl::unexpected(strerror(errno));
    }

    return (size_t)bytes_sent;
}


Socket::~Socket() {
    if (fd != -1) {
        close(fd);
    }
}

const char* Socket::listen(const int backlog) {
    return ::listen(fd, backlog) ? strerror(errno) : nullptr;
}

tl::expected<Connection, const char*> Socket::accept() {
    sockaddr_storage their_address;
    socklen_t address_size = sizeof(their_address);
    int connection_fd = ::accept(fd, (sockaddr*)&their_address, &address_size);
    if (connection_fd == -1) {
        return tl::unexpected(strerror(errno));
    }

    return Connection(connection_fd);
}
