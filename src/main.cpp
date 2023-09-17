#include <cmath>
#include <limits>
#include <stdio.h>
#include <string>

#include "common.hpp"
#include "socket.hpp"

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
const size_t NUMBER_STRING_LENGTH = std::ceil(std::log10(std::numeric_limits<size_t>::max()));
const char HTTP_HEADER_END[] = "\r\n\r\n";
const auto REMAINING_HTTP_HEADER_LENGTH = sizeof(HTTP_CONTENT_LENGTH_HEADER) - 1 + NUMBER_STRING_LENGTH + sizeof(HTTP_HEADER_END) - 1;

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
