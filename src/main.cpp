#include <stdio.h>
#include <optional>
#include <filesystem>
#include <algorithm>

#include "common.hpp"
#include "socket.hpp"
#include "http.hpp"

const char DEFAULT_PORT[] = "3000";
const int LISTEN_BACKLOG = 10;
const std::filesystem::path FILE_SERVE_ROOT = (std::filesystem::current_path() / "public").lexically_normal();

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
const char NOT_FOUND_MESSAGE[] = "Not Found";

std::optional<std::vector<char>> get_file_contents(const std::string& uri) {
    if (uri.size() == 0 || uri[0] != '/') return {};

    auto path = std::filesystem::weakly_canonical(FILE_SERVE_ROOT / uri.substr(1));

    auto first_mismatch = std::mismatch(FILE_SERVE_ROOT.begin(), FILE_SERVE_ROOT.end(), path.begin()).first;
    if (first_mismatch != FILE_SERVE_ROOT.end()) {
        return {};
    }

    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        return {};
    }

    if (fseek(file, 0, SEEK_END)) {
        return {};
    }
    const size_t length = ftell(file);
    if (fseek(file, 0, SEEK_SET)) {
        return {};
    }

    std::vector<char> contents(length);
    const auto read_bytes = fread(contents.data(), 1, length, file);
    if (read_bytes != length) {
        return {};
    }

    return contents;
}

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

    HttpResponseHeader not_found_h;
    not_found_h.status = 404;
    not_found_h["Connection"] = "close";
    not_found_h.set_content_length(sizeof(NOT_FOUND_MESSAGE) - 1);
    auto not_found_response = not_found_h.build();
    not_found_response.append(NOT_FOUND_MESSAGE);

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
        } else if (const auto file = get_file_contents(request->uri)) {
            HttpResponseHeader h;
            h["Connection"] = "close";
            h["Content-Type"] = "text/html";
            h.set_content_length(file->size());
            const auto header = h.build();

            if (auto error = connection->send(header)) {
                fprintf(stderr, "send: %s\n", error);
                continue;
            }

            if (auto error = connection->send(*file)) {
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
