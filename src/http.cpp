#include "http.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <string_view>
#include <optional>
#include <fmt/format.h>
#include "socket.hpp"
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

    Connection& connection;
    RingBuffer b;
    size_t p = 0, end = 0;

    HttpRequestParser(Connection& connection, RingBuffer&& buffer) : connection(connection), b(std::move(buffer)) {}

    static tl::expected<HttpRequestParser, const char*> create(Connection& connection) {
        auto buffer = RingBuffer::create(MIN_BUFFER_LENGTH);
        if (!buffer) {
            return tl::unexpected(buffer.error());
        }

        return HttpRequestParser(connection, std::move(*buffer));
    }

    bool ensure_data(size_t length) {
        if (p + length <= end) return true;

        if (!b.is_in_range(end - 1 + RECEIVE_CHUNK_SIZE)) {
            return false;
        }

        auto received_bytes = connection.receive(&b[end], RECEIVE_CHUNK_SIZE);
        if (!received_bytes) {
            return false;
        }

        end += *received_bytes;
        return p + length < end;
    }

    bool advance(size_t step = 1) {
        if (ensure_data(step)) {
            p += step;
            return true;
        } else {
            p = end;
            return false;
        }
    }

    void normalize() {
        bool was_done = done();

        p = b.normalized_index(p);
        end = b.normalized_index(end);

        if (!was_done && end == 0) {
            end = b.length;
        }
    }

    bool done() {
        return p == end;
    }

    void eat_whitespace() {
        while (ensure_data(1)) {
            if (!isspace(b[p])) return;
            advance();
        }
    }

    bool read_newline() {
        if (!ensure_data(2)) return false;

        if (b[p] == '\r' && b[p + 1] == '\n') {
            p += 2;
            return true;
        }

        return false;
    }

    std::string_view read_until_whitespace() {
        size_t start = p;

        while (ensure_data(1)) {
            if (isspace(b[p])) break;
            advance();
        }

        return { &b[start], p - start };
    }

    std::string_view read_line() {
        size_t start = p;
        bool found_line_break = false;

        while (ensure_data(2)) {
            if (b[p] == '\r' && b[p + 1] == '\n') {
                found_line_break = true;
                break;
            }
            advance();
        }

        size_t length = p - start;
        if (found_line_break) p += 2;

        return { &b[start], length };
    }

    std::optional<std::string_view> read_header_name() {
        size_t start = p;

        while (ensure_data(1)) {
            if (b[p] == ':') break;
            advance();
        }

        if (done()) return {};

        return {{ &b[start], p++ - start }};
    }

    std::string read_request_target_returning_path() {
        auto request_target = read_until_whitespace();

        auto it = request_target.begin();
        while (it != request_target.end() && *it != '/') {
            ++it;
        }
        if (it == request_target.end()) return "/";

        std::string path = "/";

        while (++it != request_target.end()) {
            if (*it == '?') break;
            if (*it != '%') {
                path.push_back(*it);
            } else {
                if (++it == request_target.end()) return "/"; // Invalid URI
                if (*it == '%') {
                    path.push_back('%');
                    continue;
                }

                char hex[3] = { '\0' };
                hex[0] = *it;

                if (++it == request_target.end()) return "/"; // Invalid URI
                hex[1] = *it;

                auto value = strtoul(hex, nullptr, 16);
                assert(value <= std::numeric_limits<char>::max());
                path.push_back(value);
            }
        }

        return path;
    }
};

tl::expected<HttpRequest, const char*> HttpRequest::receive(Connection& connection) {
    HttpRequest request;
    auto parser_creation = HttpRequestParser::create(connection);
    if (!parser_creation) {
        return tl::unexpected(parser_creation.error());
    }
    auto& parser = *parser_creation;

    auto method_string = parser.read_until_whitespace();
    auto method_search = STRING_METHOD_MAP.find(method_string);
    if (method_search == STRING_METHOD_MAP.end()) {
        return tl::unexpected("Unknown method");
    }
    request.method = method_search->second;

    parser.eat_whitespace();
    request.path = parser.read_request_target_returning_path();

    parser.eat_whitespace();
    auto http_version = parser.read_until_whitespace();
    if (http_version != HTTP_VERSION_1_1) {
        return tl::unexpected("Unsupported HTTP version");
    }
    parser.read_newline();
    parser.normalize();


    while (!parser.done()) {
        if (parser.read_newline()) break;

        auto header_name = parser.read_header_name();
        if (!header_name) break;
        std::string key(*header_name);
        parser.normalize();

        parser.eat_whitespace();
        auto header_value = parser.read_line();
        if (!header_value.size()) break;

        request.headers[std::move(key)] = header_value;
        parser.normalize();
    }

    return request;
}
