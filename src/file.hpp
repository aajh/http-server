#pragma once

#include "common.hpp"
#include <filesystem>
#include <vector>
#include <map>
#include <list>
#include <chrono>
#include <string>
#include <string_view>
#include <functional>
#include <tl/expected.hpp>

const char FILE_FOLDER[] = "public";

struct File {
    std::vector<char> contents;
    std::filesystem::file_time_type last_write;
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

tl::expected<std::filesystem::path, FileReadError> get_filesystem_path_from_uri_path(const std::string& uri_path);
tl::expected<File, FileReadError> read_file_contents(const std::filesystem::path& path);


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

    List file_list;
    std::map<std::reference_wrapper<const std::filesystem::path>, List::iterator, std::less<const std::filesystem::path>> file_map;

    tl::expected<std::reference_wrapper<const File>, FileReadError> get_or_read(const std::string& uri_path);
    tl::expected<std::reference_wrapper<const File>, FileReadError> latest_file() const;
};
