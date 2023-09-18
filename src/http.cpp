#include "http.hpp"

#include <cstddef>
#include <map>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <fmt/format.h>
#include "socket.hpp"

static const char HTTP_VERSION_1_1[] = "HTTP/1.1";

static const std::unordered_map<u16, std::string> STATUS_REASON_PHRASES = {
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

void HttpResponseHeader::set_content_length(size_t length) {
    headers["Content-Length"] = fmt::format("{}", length);
}

std::string HttpResponseHeader::build_header() const {
    auto res = fmt::memory_buffer();
    auto inserter = std::back_inserter(res);

    auto reason_phrase_search = STATUS_REASON_PHRASES.find(status);
    auto reason_phrase = reason_phrase_search != STATUS_REASON_PHRASES.end() ? reason_phrase_search->second : "UNKNOWN";
    fmt::format_to(inserter, "{} {} {}\r\n", HTTP_VERSION_1_1, status, reason_phrase);

    for (const auto& h : headers) {
        fmt::format_to(inserter, "{}: {}\r\n", h.first, h.second);
    }

    fmt::format_to(inserter, "\r\n");

    return fmt::to_string(res);
}


#define METHOD_STRING(method) { method, #method },
const std::unordered_map<HttpMethod, std::string> METHOD_STRING_MAP = {
    LIST_OF_HTTP_METHODS(METHOD_STRING)
};

#define STRING_METHOD(method) { #method, method },
static const std::map<std::string, HttpMethod, std::less<>> STRING_METHOD_MAP = {
    LIST_OF_HTTP_METHODS(STRING_METHOD)
};

const std::string INVALID_METHOD_STRING = "Invalid method";
const std::string& to_string(HttpMethod method) {
    auto method_search = METHOD_STRING_MAP.find(method);
    if (method_search == METHOD_STRING_MAP.end()) {
        return INVALID_METHOD_STRING;
    }
    return method_search->second;
}

struct HttpRequestParser {
    const char* p, *end;

    HttpRequestParser(const char* buffer, size_t length) : p(buffer), end(p + length) {}

    bool done() {
        return p == end;
    }

    void eat_whitespace() {
        while (p != end) {
            if (!isspace(*p)) return;
            ++p;
        }
    }

    bool read_newline() {
        if (end - p >= 2 && *p == '\r' && *(p + 1) == '\n') {
            p += 2;
            return true;
        }

        return false;
    }

    std::string_view read_until_whitespace() {
        const char* start = p;

        while (p != end) {
            if (isspace(*p)) break;
            ++p;
        }

        return { start, (size_t)(p - start) };
    }

    std::string_view read_line() {
        const char* start = p;
        bool found_line_break = false;

        while (p < end - 1) {
            if (*p == '\r' && *(p + 1) == '\n') {
                found_line_break = true;
                break;
            }
            ++p;
        }

        if (found_line_break || p == end - 1) ++p;

        ptrdiff_t diff = p - start - found_line_break;
        size_t length = diff > 0 ? diff : 0;
        return { start, length };
    }

    std::optional<std::string_view> read_header_name() {
        const char* start = p;

        while (p != end) {
            if (*p == ':') break;
            ++p;
        }

        if (p == end) return {};

        return {{ start, (size_t)(p++ - start) }};
    }

    void read_body_into(std::vector<char>& array) {
        array.insert(array.end(), p, end);
    }
};

tl::expected<HttpRequest, const char*> HttpRequest::receive(Connection& connection) {
    std::vector<char> buffer(64*1024);

    // TODO: Receive more data, if the request wasn't read completly in one go
    auto bytes_received = connection.receive(buffer.data(), buffer.size());
    if (!bytes_received) {
        return tl::unexpected(bytes_received.error());
    }

    if (*bytes_received == 0) {
        return tl::unexpected("No data received");
    }

    //fwrite(buffer.data(), *bytes_received, 1, stdout);

    HttpRequest request;
    auto parser = HttpRequestParser(buffer.data(), *bytes_received);

    auto method_string = parser.read_until_whitespace();
    auto method_search = STRING_METHOD_MAP.find(method_string);
    if (method_search == STRING_METHOD_MAP.end()) {
        return tl::unexpected("Unknown method");
    }
    request.method = method_search->second;

    parser.eat_whitespace();
    request.uri = parser.read_until_whitespace();

    parser.eat_whitespace();
    auto http_version = parser.read_until_whitespace();
    if (http_version != HTTP_VERSION_1_1) {
        return tl::unexpected("Unsupported HTTP version");
    }

    while (!parser.done()) {
        parser.eat_whitespace();
        auto header_name = parser.read_header_name();
        if (!header_name) break;

        parser.eat_whitespace();
        auto header_value = parser.read_line();
        if (!header_value.size()) break;

        request.headers[std::string(*header_name)] = header_value;
    }

    if (parser.read_newline() && !parser.done()) {
        parser.read_body_into(request.body);
    }

    return request;
}
