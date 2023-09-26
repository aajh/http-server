#pragma once

#include "common.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <filesystem>
#include <tl/expected.hpp>
#include <fmt/chrono.h>

struct Connection;

extern const std::string_view UNKNOWN_STATUS;

struct HttpResponseHeader {
    u16 status = 200;
    std::unordered_map<std::string, std::string> headers;

    HttpResponseHeader() = default;

    std::string& operator[](const std::string& key) {
        return headers[key];
    }
    std::string& operator[](std::string&& key) {
        return headers[key];
    }

    void set_content_length(size_t length);
    void set_last_modified(const std::filesystem::file_time_type& time) {
        using namespace std::chrono;
#if __cplusplus == 202002L
        const auto system_time = clock_cast<system_clock>(time);
#else
        const auto system_time = time_point_cast<system_clock::duration>(time - std::filesystem::file_time_type::clock::now() + system_clock::now());
#endif
        set_last_modified(system_time);
    }
    template<class T> void set_last_modified(const T& time) {
        headers["Last-Modified"] = fmt::format("{:%a, %d %b %Y %H:%M:%S} GMT", std::chrono::floor<std::chrono::seconds>(time));
    }

    const std::string_view& status_to_string() const;
    std::string build() const;

    static std::string build_error(u16 status);
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

extern const std::string_view INVALID_METHOD_STRING;
const std::string_view& to_string(HttpMethod method);

struct HttpRequest {
    HttpMethod method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;

    enum ReceiveError {
        SERVER_ERROR,
        UNKNOWN_METHOD,
        UNSUPPORTED_HTTP_VERSION,
        BAD_REQUEST,
        PAYLOAD_TOO_LARGE,
    };
    static tl::expected<HttpRequest, ReceiveError> receive(Connection&);
};
