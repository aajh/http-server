#include <cstdlib>
#include <limits>
#include <fmt/core.h>

#include "common.hpp"
#include "http.hpp"
#include "file.hpp"

const u16 DEFAULT_PORT = 3000;

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

awaitable<void> handle_connection(asio::ip::tcp::socket socket, FileCache& file_cache) {
    try {
        auto executor = co_await this_coro::executor;

        auto request = co_await HttpRequest::receive(socket);
        if (!request) {
            fmt::print(stderr, "Error while receiving the request: ");
            u16 status;
            switch (request.error()) {
                case HttpRequest::SERVER_ERROR:
                    fmt::print(stderr, "server error\n");
                    status = 500;
                    break;
                case HttpRequest::UNKNOWN_METHOD:
                    fmt::print(stderr, "unknown method\n");
                    status = 501;
                    break;
                case HttpRequest::UNSUPPORTED_HTTP_VERSION:
                    fmt::print(stderr, "unsupported HTTP version\n");
                    status = 505;
                    break;
                case HttpRequest::BAD_REQUEST:
                    fmt::print(stderr, "bad request\n");
                    status = 400;
                    break;
                case HttpRequest::PAYLOAD_TOO_LARGE:
                    fmt::print(stderr, "payload too large\n");
                    status = 413;
                    break;
            }

            auto response = HttpResponseHeader::build_error(status);
            co_await async_write(socket, asio::buffer(response), use_awaitable);
            co_return;
        }

        fmt::print("Method: {}\n", to_string(request->method));
        fmt::print("Path: {}\n", request->path);
        fmt::print("Headers:\n");
        for (const auto& h : request->headers) {
            fmt::print("{}: {}\n", h.first, h.second);
        }


        if (request->path == "/" || request->path == "/index.html") {
            auto content_length = sizeof(DEFAULT_HTML_DOCUMENT) - 1;
            HttpResponseHeader h;
            h["Connection"] = "close";
            h["Content-Type"] = "text/html";
            h.set_content_length(content_length);
            auto header = h.build();
            co_await async_write(socket, asio::buffer(header), use_awaitable);
            co_await async_write(socket, asio::buffer(DEFAULT_HTML_DOCUMENT, content_length), use_awaitable);
        } else {
            const auto file_result = file_cache.get_or_read(request->path);

            if (!file_result) {
                const auto& error = file_result.error();

                u16 status = 500;
                switch (error.type) {
                    case FileReadError::OK:
                        break;
                    case FileReadError::INVALID_URI:
                        status = 400;
                        break;
                    case FileReadError::NOT_FOUND:
                        status = 404;
                        break;
                    case FileReadError::IO_ERROR:
                        status = 500;
                        if (error.message) {
                            fmt::print(stderr, "IO error: {}\n", error.message);
                        } else if (error.ec) {
                            fmt::print(stderr, "IO error: {}\n", error.ec.message());
                        } else {
                            fmt::print(stderr, "Unknown IO error\n");
                        }
                        break;
                }

                auto response = HttpResponseHeader::build_error(status);
                co_await async_write(socket, asio::buffer(response), use_awaitable);
                co_return;
            }

            const auto& file = file_result->get();

            HttpResponseHeader h;
            h["Connection"] = "close";
            h["Content-Type"] = file.mime_type;
            h.set_content_length(file.contents.size());
            h.set_last_modified(file.last_write);
            const auto header = h.build();

            co_await async_write(socket, asio::buffer(header), use_awaitable);
            co_await async_write(socket, asio::buffer(file.contents), use_awaitable);
        }
    } catch (std::exception& e) {
        fmt::print("Error: {}\n", e.what());
    }
}

awaitable<void> listener(u16 port) {
    try {
        auto executor = co_await this_coro::executor;

        FileCache file_cache;

        asio::ip::tcp::acceptor acceptor(executor, {asio::ip::tcp::v4(), port}, false);
        fmt::print("Listening on port {}...\n", port);

        while (true) {
            auto socket = co_await acceptor.async_accept(use_awaitable);
            auto remote_endpoint = socket.remote_endpoint();
            fmt::print("\nNew connection from address: {}:{}\n", remote_endpoint.address().to_string(), remote_endpoint.port());
            co_spawn(executor, handle_connection(std::move(socket), file_cache), detached);
        }
    } catch (std::exception& e) {
        fmt::print("Error: {}\n", e.what());
    }
}

int main(int argc, char** argv) {
    auto port = argc > 1 ? strtoul(argv[1], nullptr, 10) : std::numeric_limits<u32>::max();
    if (port > std::numeric_limits<u16>::max()) {
        port = DEFAULT_PORT;
    }

    try {
        asio::io_context io_context(1);
        co_spawn(io_context, listener(port), detached);
        io_context.run();
    } catch (std::exception& e) {
        fmt::print("Error: {}\n", e.what());
    }

    return 0;
}
