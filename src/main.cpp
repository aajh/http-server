#include <stdio.h>
#include <fmt/core.h>

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
        fmt::print(stderr, "Failed to bind and listen to port {}: {}\n", port, socket.error());
        return -1;
    }

    fmt::print("Listening on port {}...\n", port);

    FileCache file_cache;

    HttpResponseHeader h;
    h["Connection"] = "close";
    h["Content-Type"] = "text/html";
    h.set_content_length(sizeof(DEFAULT_HTML_DOCUMENT) - 1);
    auto root_response = h.build();
    root_response.append(DEFAULT_HTML_DOCUMENT);

    while (true) {
        auto connection = socket->accept();
        if (!connection) {
            fmt::print(stderr, "accept: {}\n", connection.error());
            continue;
        }

        fmt::print("\nConnection from address {}\n", connection->ip);

        auto request = HttpRequest::receive(*connection);
        if (!request) {
            fmt::print(stderr, "Error while receiving the request: ");

            HttpResponseHeader h;
            h["Connection"] = "close";

            switch (request.error()) {
                case HttpRequest::SERVER_ERROR:
                    fmt::print(stderr, "server error\n");
                    h.status = 500;
                    break;
                case HttpRequest::UNKNOWN_METHOD:
                    fmt::print(stderr, "unknown method\n");
                    h.status = 501;
                    break;
                case HttpRequest::UNSUPPORTED_HTTP_VERSION:
                    fmt::print(stderr, "unsupported HTTP version\n");
                    h.status = 505;
                    break;
                case HttpRequest::BAD_REQUEST:
                    fmt::print(stderr, "bad request\n");
                    h.status = 400;
                    break;
            }

            const auto& message = h.status_to_string();
            h.set_content_length(message.size());

            auto response = h.build();
            response.append(message);
            if (auto error = connection->send(response)) {
                fmt::print(stderr, "send: {}\n", error);
            }

            continue;
        }


        fmt::print("Method: {}\n", to_string(request->method));
        fmt::print("Path: {}\n", request->path);
        fmt::print("Headers:\n");
        for (const auto& h : request->headers) {
            fmt::print("{}: {}\n", h.first, h.second);
        }


        if (request->path == "/" || request->path == "/index.html") {
            if (auto error = connection->send(root_response)) {
                fmt::print(stderr, "send: {}\n", error);
                continue;
            }
        } else {
            const auto file_result = file_cache.get_or_read(request->path);

            if (!file_result) {
                const auto& error = file_result.error();

                HttpResponseHeader h;
                h.status = 500;
                h["Connection"] = "close";

                switch (error.type) {
                    case FileReadError::OK:
                        break;
                    case FileReadError::INVALID_URI:
                        h.status = 400;
                        break;
                    case FileReadError::NOT_FOUND:
                        h.status = 404;
                        break;
                    case FileReadError::IO_ERROR:
                        h.status = 500;
                        if (error.message) {
                            fmt::print(stderr, "IO error: {}\n", error.message);
                        } else if (error.ec) {
                            fmt::print(stderr, "IO error: {}\n", error.ec.message());
                        } else {
                            fmt::print(stderr, "Unknown IO error\n");
                        }
                        break;
                }

                const auto& message = h.status_to_string();
                h.set_content_length(message.size());

                auto response = h.build();
                response.append(message);
                if (auto error = connection->send(response)) {
                    fmt::print(stderr, "send: {}\n", error);
                }

                continue;
            }

            const auto& file = file_result->get();

            HttpResponseHeader h;
            h["Connection"] = "close";
            h["Content-Type"] = file.mime_type;
            h.set_content_length(file.contents.size());
            h.set_last_modified(file.last_write);
            const auto header = h.build();

            if (auto error = connection->send(header)) {
                fmt::print(stderr, "send: {}\n", error);
                continue;
            }

            if (auto error = connection->send(file.contents)) {
                fmt::print(stderr, "send: {}\n", error);
                continue;
            }
        }
    }

    return 0;
}
