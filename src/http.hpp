#pragma once

#include "common.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <tl/expected.hpp>

struct Connection;

extern const std::string UNKNOWN_STATUS;

struct HttpResponseHeader {
    u16 status = 200;
    std::unordered_map<std::string, std::string> headers;

    std::string& operator[](const std::string& key) {
        return headers[key];
    }
    std::string& operator[](std::string&& key) {
        return headers[key];
    }

    void set_content_length(size_t length);

    const std::string& status_to_string() const;
    std::string build() const;
};

#define LIST_OF_HTTP_METHODS(DO)\
    DO(GET)\
    DO(HEAD)\
    DO(POST)\
    DO(PUT)\
    DO(DELETE)\
    DO(CONNECT)\
    DO(OPTIONS)\
    DO(TRACE)\
    DO(PATCH)\

#define METHOD_ENUM(method) method,
enum HttpMethod {
    LIST_OF_HTTP_METHODS(METHOD_ENUM)
};

extern const std::string INVALID_METHOD_STRING;
const std::string& to_string(HttpMethod method);

struct HttpRequest {
    HttpMethod method;
    std::string uri;
    std::unordered_map<std::string, std::string> headers;

    static tl::expected<HttpRequest, const char*> receive(Connection&);
};
