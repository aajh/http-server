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
    auto root_response = h.build_header();
    root_response.append(HTML_DOCUMENT);

    HttpResponseHeader not_found_h;
    not_found_h.status = 404;
    not_found_h["Connection"] = "close";
    const auto not_found_response = not_found_h.build_header();

    while (true) {
        auto connection = socket->accept();
        if (!connection) {
            fprintf(stderr, "accept: %s\n", connection.error());
            continue;
        }

        printf("\nConnection from address %s\n", connection->ip);

        auto request = HttpRequest::receive(*connection);
        if (!request) {
            fprintf(stderr, "Error while receiving the request: %s\n", request.error());
            continue;
        }


        printf("Method: %s\n", to_string(request->method).data());
        printf("URI: %s\n",request->uri.data());

        printf("Headers:\n");
        for (const auto& h : request->headers) {
            printf("%s: %s\n", h.first.data(), h.second.data());
        }
        if (request->body.size()) {
            printf("The request has body with length of %lu\n", request->body.size());
        } else {
            printf("The request has no body\n");
        }


        if (request->uri == "/") {
            if (auto error = connection->send(root_response)) {
                fprintf(stderr, "send: %s\n", error);
                continue;
            }
        } else {
            if (auto error = connection->send(not_found_response)) {
                fprintf(stderr, "send: %s\n", error);
                continue;
            }
        }
    }

    return 0;
}
