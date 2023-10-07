#include "http.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <string_view>
#include <optional>
#include <fmt/format.h>
#include "ring_buffer.hpp"

static const char HTTP_VERSION_1_1[] = "HTTP/1.1";

static const std::unordered_map<u16, std::string_view> STATUS_REASON_PHRASES = {
    { 100, "Continue" },
    { 101, "Switching Protocols" },
    { 102, "Processing" },
    { 103, "Early Hints" },
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 203, "Non-Authoritative Information" },
    { 204, "No Content" },
    { 205, "Reset Content" },
    { 206, "Partial Content" },
    { 207, "Multi-Status" },
    { 208, "Already Reported" },
    { 226, "IM Used" },
    { 300, "Multiple Choices" },
    { 301, "Moved Permanently" },
    { 302, "Found" },
    { 303, "See Other" },
    { 304, "Not Modified" },
    { 305, "Use Proxy" },
    { 306, "Switch Proxy" },
    { 307, "Temporary Redirect" },
    { 308, "Permanent Redirect" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 402, "Payment Required" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 405, "Method Not Allowed" },
    { 406, "Not Acceptable" },
    { 407, "Proxy Authentication Required" },
    { 408, "Request Timeout" },
    { 409, "Conflict" },
    { 410, "Gone" },
    { 411, "Length Required" },
    { 412, "Precondition Failed" },
    { 413, "Payload Too Large" },
    { 414, "URI Too Long" },
    { 415, "Unsupported Media Type" },
    { 416, "Range Not Satisfiable" },
    { 417, "Expectation Failed" },
    { 418, "I'm a teapot" },
    { 421, "Misdirected Request" },
    { 422, "Unprocessable Entity" },
    { 423, "Locked" },
    { 424, "Failed Dependency" },
    { 425, "Too Early" },
    { 426, "Upgrade Required" },
    { 428, "Precondition Required" },
    { 429, "Too Many Requests" },
    { 431, "Request Header Fields Too Large" },
    { 451, "Unavailable For Legal Reasons" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 504, "Gateway Timeout" },
    { 505, "HTTP Version Not Supported" },
    { 506, "Variant Also Negotiates" },
    { 507, "Insufficient Storage" },
    { 508, "Loop Detected" },
    { 510, "Not Extended" },
    { 511, "Network Authentication Required" },
};
const std::string_view UNKNOWN_STATUS = "Unknown Status";

void HttpResponseHeader::set_content_length(size_t length) {
    headers["Content-Length"] = fmt::format("{}", length);
}

const std::string_view& HttpResponseHeader::status_to_string() const {
    auto reason_phrase_search = STATUS_REASON_PHRASES.find(status);
    return reason_phrase_search != STATUS_REASON_PHRASES.end() ? reason_phrase_search->second : UNKNOWN_STATUS;
}

std::string HttpResponseHeader::build() const {
    auto res = fmt::memory_buffer();
    auto inserter = std::back_inserter(res);

    auto reason_phrase = status_to_string();
    fmt::format_to(inserter, "{} {} {}\r\n", HTTP_VERSION_1_1, status, reason_phrase);

    for (const auto& h : headers) {
        fmt::format_to(inserter, "{}: {}\r\n", h.first, h.second);
    }

    fmt::format_to(inserter, "\r\n");

    return fmt::to_string(res);
}

std::string HttpResponseHeader::build_error(u16 status) {
        HttpResponseHeader h;
        h.status = status;
        h["Connection"] = "close";
        h["Content-Type"] = "text/html";

        const auto& message = h.status_to_string();
        h.set_content_length(message.size());

        auto response = h.build();
        response.append(message);

        return response;
}


#define METHOD_STRING(method) { method, #method },
static const std::unordered_map<HttpMethod, std::string_view> METHOD_STRING_MAP = {
    LIST_OF_HTTP_METHODS(METHOD_STRING)
};

#define STRING_METHOD(method) { #method, method },
static const std::map<std::string_view, HttpMethod, std::less<>> STRING_METHOD_MAP = {
    LIST_OF_HTTP_METHODS(STRING_METHOD)
};

const std::string_view INVALID_METHOD_STRING = "Invalid method";
const std::string_view& to_string(HttpMethod method) {
    auto method_search = METHOD_STRING_MAP.find(method);
    if (method_search == METHOD_STRING_MAP.end()) {
        return INVALID_METHOD_STRING;
    }
    return method_search->second;
}

struct HttpRequestParser {
    static constexpr size_t MAX_TOKEN_LENGTH = 8*1024;
    static constexpr size_t MIN_BUFFER_LENGTH = 2*MAX_TOKEN_LENGTH;
    static constexpr size_t RECEIVE_CHUNK_SIZE = MAX_TOKEN_LENGTH;

    enum Error {
        OK = 0,
        PAYLOAD_TOO_LARGE,
        SERVER_ERROR,
        BAD_REQUEST,
    };

    asio::ip::tcp::socket& connection;
    RingBuffer b;
    size_t p = 0, end = 0;
    ssize_t token_start = -1;

    HttpRequestParser(asio::ip::tcp::socket& connection, RingBuffer&& buffer) : connection(connection), b(std::move(buffer)) {}

    static tl::expected<HttpRequestParser, const char*> create(asio::ip::tcp::socket& connection) {
        auto buffer = RingBuffer::create(MIN_BUFFER_LENGTH);
        if (!buffer) {
            return tl::unexpected(buffer.error());
        }

        return HttpRequestParser(connection, std::move(*buffer));
    }

    static bool is_whitespace(char c) {
        return c == ' ' || c == '\t';
    }

    static bool is_whitespace_or_line_break(char c) {
        return is_whitespace(c) || c == '\r' || c == '\n';
    }

    awaitable<Error> ensure_data(size_t length) {
        if (p + length <= end) co_return OK;

        if (!b.is_in_range(end - 1 + RECEIVE_CHUNK_SIZE)) {
            co_return PAYLOAD_TOO_LARGE;
        }

        size_t total_received_bytes = 0;
        while (total_received_bytes < length) {
            boost::system::error_code ec;
            auto received_bytes = co_await connection.async_receive(asio::buffer(&b[end], RECEIVE_CHUNK_SIZE), RE(ec));
            if (ec) {
                co_return ec == asio::error::eof || ec == asio::error::connection_reset ? BAD_REQUEST : SERVER_ERROR;
            }
            if (received_bytes == 0) {
                co_return BAD_REQUEST;
            }
            total_received_bytes += received_bytes;
        }

        if (token_start >= 0 && (end - (size_t)token_start) > 0) {
            auto n_start = b.normalized_index((size_t)token_start);
            auto n_end = b.normalized_index(end);
            auto n_new_end = b.normalized_index(end + total_received_bytes);
            bool end_wrapped = n_new_end <= n_end;

            bool overwritten;
            if (n_start <= n_end) {
                overwritten = end_wrapped && n_start < n_new_end;
            } else {
                // n_start > n_end
                overwritten = end_wrapped || n_start < n_new_end;
            }

            if (overwritten) co_return PAYLOAD_TOO_LARGE;
        }

        end += total_received_bytes;
        if (token_start == -1) {
            normalize();
        }
        co_return OK;
    }

    void normalize() {
        bool was_empty = empty();

        p = b.normalized_index(p);
        end = b.normalized_index(end);

        if (!was_empty && end == 0) {
            end = b.length;
        }
        if (end < p) {
            end += b.length;
            assert(end >= p);
        }
    }

    bool empty() {
        return p == end;
    }

    std::string_view get_current_token() {
        if (token_start == -1) return {};

        auto start = token_start;
        auto length = p - start;

        token_start = -1;
        normalize();

        return { &b[start], length };
    }

    awaitable<Error> eat_whitespace() {
        while (true) {
            if (auto error = co_await ensure_data(1)) co_return error;
            if (!is_whitespace(b[p])) co_return OK;
            ++p;
        }
    }

    awaitable<tl::expected<bool, Error>> maybe_read_newline() {
        if (auto error = co_await ensure_data(2)) co_return tl::unexpected(error);

        if (b[p] == '\r' && b[p + 1] == '\n') {
            p += 2;
            co_return true;
        }

        co_return false;
    }

    awaitable<tl::expected<std::string_view, Error>> read_until_whitespace() {
        token_start = p;

        while (true) {
            if (auto error = co_await ensure_data(1)) co_return tl::unexpected(error);
            if (is_whitespace_or_line_break(b[p])) break;
            ++p;
        }

        co_return get_current_token();
    }

    awaitable<tl::expected<std::string_view, Error>> read_line() {
        token_start = p;

        while (true) {
            if (auto error = co_await ensure_data(2)) co_return tl::unexpected(error);
            if (b[p] == '\r' && b[p + 1] == '\n') break;
            ++p;
        }

        auto token = get_current_token();
        p += 2;
        co_return token;
    }

    awaitable<tl::expected<std::string_view, Error>> read_header_name() {
        token_start = p;

        while (true) {
            if (auto error = co_await ensure_data(1)) co_return tl::unexpected(error);
            if (b[p] == ':') break;
            ++p;
        }

        auto token = get_current_token();
        ++p;

        if (!token.size() || is_whitespace_or_line_break(token[token.size() - 1])) {
            co_return tl::unexpected(BAD_REQUEST);
        }

        co_return token;
    }

    awaitable<tl::expected<std::string_view, Error>> read_header_field() {
        auto line = co_await read_line();
        if (!line) co_return tl::unexpected(line.error());
        auto& field = *line;

        while (field.size() && is_whitespace(field[field.size() - 1])) {
            field.remove_suffix(1);
        }

        if (!field.size()) co_return tl::unexpected(BAD_REQUEST);
        co_return field;
    }

    awaitable<tl::expected<std::string, Error>> read_request_target_returning_path() {
        auto token = co_await read_until_whitespace();
        if (!token) co_return tl::unexpected(token.error());
        auto& request_target = *token;

        auto it = request_target.begin();
        while (it != request_target.end() && *it != '/') {
            ++it;
        }
        if (it == request_target.end()) co_return "/";

        std::string path = "/";

        while (++it != request_target.end()) {
            if (*it == '?') break;
            if (*it != '%') {
                path.push_back(*it);
            } else {
                if (++it == request_target.end()) co_return "/"; // Invalid URI
                if (*it == '%') {
                    path.push_back('%');
                    continue;
                }

                char hex[3] = { '\0' };
                hex[0] = *it;

                if (++it == request_target.end()) co_return "/"; // Invalid URI
                hex[1] = *it;

                auto value = strtoul(hex, nullptr, 16);
                assert(value <= std::numeric_limits<char>::max());
                path.push_back(value);
            }
        }

        co_return path;
    }
};

static HttpRequest::ReceiveError parse_error_to_receive_error(HttpRequestParser::Error parse_error) {
    using R = HttpRequest;
    using P = HttpRequestParser;
    switch (parse_error) {
        case P::OK:
            return R::SERVER_ERROR;
        case P::PAYLOAD_TOO_LARGE:
            return R::PAYLOAD_TOO_LARGE;
        case P::SERVER_ERROR:
            return R::SERVER_ERROR;
        case P::BAD_REQUEST:
            return R::BAD_REQUEST;
    }
    return R::SERVER_ERROR;
}
awaitable<tl::expected<HttpRequest, HttpRequest::ReceiveError>> HttpRequest::receive(asio::ip::tcp::socket& connection) {
    HttpRequest request;
    auto parser_creation = HttpRequestParser::create(connection);
    if (!parser_creation) {
        co_return tl::unexpected(SERVER_ERROR);
    }
    auto& parser = *parser_creation;

    if (auto result = co_await parser.maybe_read_newline(); !result) {
        co_return tl::unexpected(parse_error_to_receive_error(result.error()));
    }
    auto method_string = co_await parser.read_until_whitespace();
    if (!method_string) co_return tl::unexpected(parse_error_to_receive_error(method_string.error()));
    auto method_search = STRING_METHOD_MAP.find(*method_string);
    if (method_search == STRING_METHOD_MAP.end()) {
        co_return tl::unexpected(UNKNOWN_METHOD);
    }
    request.method = method_search->second;

    if (auto error = co_await parser.eat_whitespace()) {
        co_return tl::unexpected(parse_error_to_receive_error(error));
    }
    auto path = co_await parser.read_request_target_returning_path();
    if (!path) co_return tl::unexpected(parse_error_to_receive_error(path.error()));
    request.path = *path;

    if (auto error = co_await parser.eat_whitespace()) {
        co_return tl::unexpected(parse_error_to_receive_error(error));
    }
    auto http_version = co_await parser.read_until_whitespace();
    if (!http_version) {
        co_return tl::unexpected(parse_error_to_receive_error(http_version.error()));
    }
    if (*http_version != HTTP_VERSION_1_1) {
        co_return tl::unexpected(UNSUPPORTED_HTTP_VERSION);
    }

    if (auto result = co_await parser.maybe_read_newline(); !result) {
        co_return tl::unexpected(parse_error_to_receive_error(result.error()));
    } else if (!*result) {
        co_return tl::unexpected(BAD_REQUEST);
    }

    while (true) {
        if (auto read_newline = co_await parser.maybe_read_newline(); !read_newline) {
            co_return tl::unexpected(parse_error_to_receive_error(read_newline.error()));
        } else if (*read_newline) {
            break;
        }

        auto header_name_result = co_await parser.read_header_name();
        if (!header_name_result) {
            co_return tl::unexpected(parse_error_to_receive_error(header_name_result.error()));
        }
        std::string header_name(*header_name_result);

        if (auto error = co_await parser.eat_whitespace()) {
            co_return tl::unexpected(parse_error_to_receive_error(error));
        }

        auto field = co_await parser.read_header_field();
        if (!field) {
            co_return tl::unexpected(parse_error_to_receive_error(field.error()));
        }
        request.headers[std::move(header_name)] = *field;
    }

    co_return request;
}
