#include "socket.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

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

tl::expected<size_t, const char*> Connection::send_raw(const char* data, size_t length) {
    ssize_t bytes_sent = ::send(fd, data, length, 0);
    if (bytes_sent == -1) {
        return tl::unexpected(strerror(errno));
    }

    return (size_t)bytes_sent;
}

const char* Connection::send(const char* data, size_t length) {
    size_t total_bytes_sent = 0;

    while (total_bytes_sent < length) {
        auto bytes_sent = send_raw(data + total_bytes_sent, length - total_bytes_sent);
        if (!bytes_sent) {
            return bytes_sent.error();
        }

        total_bytes_sent += *bytes_sent;
    }

    return nullptr;
}

tl::expected<size_t, const char*> Connection::receive(void* buffer, size_t length) {
    ssize_t received_bytes = ::recv(fd, buffer, length, 0);
    if (received_bytes == -1) {
        return tl::unexpected(strerror(errno));
    }

    return (size_t)received_bytes;
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

    auto connection = Connection(connection_fd);

    if (their_address.ss_family == AF_INET6) {
        const auto sa6 = reinterpret_cast<sockaddr_in6*>(&their_address);
        inet_ntop(AF_INET6, &(sa6->sin6_addr), connection.ip, sizeof(connection.ip));
    } else {
        const auto sa = reinterpret_cast<sockaddr_in*>(&their_address);
        inet_ntop(AF_INET, &(sa->sin_addr), connection.ip, sizeof(connection.ip));
    }

    return connection;
}
