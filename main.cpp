#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <optional>
#include <utility>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <tl/expected.hpp>

#ifndef defer
struct defer_dummy {};
template <class F> struct deferrer { F f; ~deferrer() { f(); } };
template <class F> deferrer<F> operator*(defer_dummy, F f) { return {f}; }
#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer auto DEFER(__LINE__) = defer_dummy{} *[&]()
#endif


const char PORT[] = "3000";
const int LISTEN_BACKLOG = 10;

const char HTML_DOCUMENT[] =
"<!DOCTYPE html>"
"<html lang=\"en\">"
    "<head>"
        "<meta charset=\"utf-8\" />"
        "<title>HTTP Server</title>"
    "</head>"
    "<body>"
        "<h1>Hello from my HTTP server</h1>"
    "</body>"
"</html>"
;

const char HTTP_RESPONSE_HEADER[] =
"HTTP/1.1 200 OK\r\n"
"Connection: close\r\n"
"Content-Type: text/html\r\n"
;

const char HTTP_CONTENT_LENGTH_HEADER[] = "Content-Length: ";
const size_t NUMBER_STRING_LENGTH = ceil(log10(UINT64_MAX));
const char HTTP_HEADER_END[] = "\r\n\r\n";
const auto REMAINING_HTTP_HEADER_LENGTH = sizeof(HTTP_CONTENT_LENGTH_HEADER) - 1 + NUMBER_STRING_LENGTH + sizeof(HTTP_HEADER_END) - 1;

struct Connection {
    int fd;

    Connection(int connection_fd) : fd(connection_fd) {}
    Connection(const Connection&) = delete;
    Connection(Connection&& o) : fd(o.fd) {
        o.fd = -1;
    };
    ~Connection() {
        if (fd != -1) {
            close(fd);
        }
    }

    tl::expected<size_t, const char*> send(void *data, size_t length) {
        ssize_t bytes_sent = ::send(fd, data, length, 0);
        if (bytes_sent == -1) {
            return tl::unexpected(strerror(errno));
        }

        return (size_t)bytes_sent;
    }
};

struct Socket {
    int fd;

    Socket(int socket_fd) : fd(socket_fd) {}
    Socket(const Socket&) = delete;
    Socket(Socket&& o) : fd(o.fd) {
        o.fd = -1;
    };
    ~Socket() {
        if (fd != -1) {
            close(fd);
        }
    }

    const char* listen(const int backlog) {
        return ::listen(fd, backlog) ? strerror(errno) : nullptr;
    }

    tl::expected<Connection, const char*> accept() {
        sockaddr_storage their_address;
        socklen_t address_size = sizeof(their_address);
        int connection_fd = ::accept(fd, (sockaddr*)&their_address, &address_size);
        if (connection_fd == -1) {
            return tl::unexpected(strerror(errno));
        }

        return Connection(connection_fd);
    }

    static tl::expected<Socket, const char*> bind(const char* port) {
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
};

int main() {
    auto socket = Socket::bind_and_listen(PORT, LISTEN_BACKLOG);
    if (!socket) {
        fprintf(stderr, "Failed to bind and listen to a socket: %s\n", socket.error());
        return -1;
    }

    printf("Listening on port %s...\n", PORT);


    const size_t response_header_max_length = sizeof(HTTP_RESPONSE_HEADER) - 1 + REMAINING_HTTP_HEADER_LENGTH + 1;
    std::string response(response_header_max_length + sizeof(HTML_DOCUMENT) - 1, '\0');

    auto response_header_length = snprintf(response.data(), response_header_max_length, "%s%s%ld%s", HTTP_RESPONSE_HEADER, HTTP_CONTENT_LENGTH_HEADER, sizeof(HTML_DOCUMENT) - 1, HTTP_HEADER_END);
    if (response_header_length < 0) {
        fprintf(stderr, "Error while constructing the HTTP header with snprintf\n");
        return -1;
    }
    if ((unsigned)response_header_length > response_header_max_length - 1) {
        response_header_length = response_header_max_length - 1;
    }
    response.resize(response_header_length);

    response.append(HTML_DOCUMENT);


    while (1) {
        auto connection = socket->accept();
        if (!connection) {
            fprintf(stderr, "accept: %s\n", connection.error());
            continue;
        }

        auto bytes_sent = connection->send(response.data(), response.size());
        if (!bytes_sent) {
            fprintf(stderr, "send: %s\n", bytes_sent.error());
            continue;
        }

        if (*bytes_sent < response.size()) {
            fprintf(stderr, "Sent only %ld/%ld bytes\n", *bytes_sent, response.size());
            continue;
            // TODO: Send rest of the response in this case
        }
    }

    return 0;
}

