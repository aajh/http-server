#include <stdio.h>

#include "common.hpp"
#include "socket.hpp"
#include "http.hpp"

const char DEFAULT_PORT[] = "3000";
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

int main(int argc, char** argv) {
    auto port = argc > 1 ? argv[1] : DEFAULT_PORT;

    auto socket = Socket::bind_and_listen(port, LISTEN_BACKLOG);
    if (!socket) {
        fprintf(stderr, "Failed to bind and listen to port %s: %s\n", port, socket.error());
        return -1;
    }

    printf("Listening on port %s...\n", port);

    HttpResponseHeader h;
    h["Connection"] = "close";
    h["Content-Type"] = "text/html";
    h.set_content_length(sizeof(HTML_DOCUMENT) - 1);
    const auto header = h.build_header();

    while (true) {
        auto connection = socket->accept();
        if (!connection) {
            fprintf(stderr, "accept: %s\n", connection.error());
            continue;
        }

        printf("Connection from address %s\n", connection->ip);

        if (auto error = connection->send(header)) {
            fprintf(stderr, "send: %s\n", error);
            continue;
        }

        if (auto error = connection->send(HTML_DOCUMENT, sizeof(HTML_DOCUMENT) - 1)) {
            fprintf(stderr, "send: %s\n", error);
            continue;
        }
    }

    return 0;
}
