#include <stdio.h>

#include "common.hpp"
#include "socket.hpp"
#include "http.hpp"
#include "file.hpp"

const char DEFAULT_PORT[] = "3000";
const int LISTEN_BACKLOG = 10;

const char DEFAULT_HTML_DOCUMENT[] =
"<!DOCTYPE html>"
"<html lang=\"en\">"
    "<head>"
        "<meta charset=\"utf-8\" />"
        "<title>HTTP Server</title>"
    "</head>"
    "<body>"
        "<h1>Hello from my HTTP server</h1>"
        "<a href=\"/file.html\">Another page</a>"
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
    h.set_content_length(sizeof(DEFAULT_HTML_DOCUMENT) - 1);
    auto root_response = h.build();
    root_response.append(DEFAULT_HTML_DOCUMENT);

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


        if (request->uri == "/" || request->uri == "/index.html") {
            if (auto error = connection->send(root_response)) {
                fprintf(stderr, "send: %s\n", error);
                continue;
            }
        } else {
            const auto file = read_file_contents(request->uri);

            if (!file.has_value()) {
                const auto& error = file.error();

                HttpResponseHeader h;
                h.status = 500;
                h["Connection"] = "close";

                switch (error.type) {
                    case FileReadError::INVALID_URI:
                        h.status = 400;
                        break;
                    case FileReadError::NOT_FOUND:
                        h.status = 404;
                        break;
                    case FileReadError::IO_ERROR:
                        h.status = 500;
                        if (error.message) {
                            fprintf(stderr, "IO error: %s\n", error.message);
                        } else if (error.ec) {
                            fprintf(stderr, "IO error: %s\n", error.ec.message().data());
                        } else {
                            fprintf(stderr, "Unknown IO error\n");
                        }
                        break;
                }

                const auto& message = h.status_to_string();
                h.set_content_length(message.size());

                auto response = h.build();
                response.append(message);
                if (auto error = connection->send(response)) {
                    fprintf(stderr, "send: %s\n", error);
                }

                continue;
            }

            HttpResponseHeader h;
            h["Connection"] = "close";
            h["Content-Type"] = "text/html";
            h.set_content_length(file->contents.size());
            const auto header = h.build();

            if (auto error = connection->send(header)) {
                fprintf(stderr, "send: %s\n", error);
                continue;
            }

            if (auto error = connection->send(file->contents)) {
                fprintf(stderr, "send: %s\n", error);
                continue;
            }
        }
    }

    return 0;
}
