#pragma once

#include "common.hpp"
#include <filesystem>
#include <vector>
#include <unordered_map>
#include <list>
#include <chrono>
#include <string>
#include <string_view>
#include <functional>
#include <tl/expected.hpp>

const char DEFAULT_MIME_TYPE[] = "application/octet-stream";

struct File {
    std::vector<char> contents;
    std::filesystem::file_time_type last_write;
    std::string mime_type = DEFAULT_MIME_TYPE;
};

struct FileReadError {
    enum Type {
        OK = 0,
        INVALID_URI,
        NOT_FOUND,
        IO_ERROR,
    } type = OK;

    std::error_code ec = {};
    const char* message = nullptr;
};

tl::expected<File, FileReadError> read_file_contents(const std::filesystem::path& path);

struct ReferenceWrappedPathHash {
    size_t operator() (const std::reference_wrapper<const std::filesystem::path>& a) const {
        return std::filesystem::hash_value(a);
    }
};

struct FileCache {
    using Clock = std::chrono::steady_clock;
    using Time = std::chrono::time_point<Clock>;
    struct Entry {
        std::filesystem::path path;
        FileReadError::Type status = FileReadError::OK;
        File file;
        Time last_accessed;
    };
    using List = std::list<Entry>;

    static constexpr size_t MEGABYTE = 1024*1024;
    static constexpr size_t MAX_CACHED_FILE_SIZE = 128*MEGABYTE;
    static constexpr size_t MAX_CACHE_SIZE = 1024*MEGABYTE;
    static constexpr size_t MAX_CACHE_ENTRIES = 1024;
    static constexpr auto MAX_ENTRY_LIFETIME = std::chrono::minutes(5);

    FileCache(const char* folder = "");

    std::filesystem::path file_root_path;
    List file_list;
    std::unordered_map<std::reference_wrapper<const std::filesystem::path>, List::iterator, ReferenceWrappedPathHash> file_map;
    size_t cache_size = 0;

    tl::expected<std::reference_wrapper<const File>, FileReadError> get_or_read(const std::string& uri_path);
    tl::expected<std::filesystem::path, FileReadError> get_filesystem_path_from_uri_path(const std::string& uri_path) const;
    tl::expected<std::reference_wrapper<const File>, FileReadError> latest_file() const;
    void trim();
};
