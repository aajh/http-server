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
tl::expected<File, FileReadError> read_file_contents(const std::string& uri);


struct FileCache {
    using Clock = std::chrono::steady_clock;
    using Time = std::chrono::time_point<Clock>;
    struct Entry {
        std::string uri;
        FileReadError::Type status = FileReadError::OK;
        File file;
        Time last_accessed;
    };
    using List = std::list<Entry>;

    List file_list;
    std::unordered_map<std::string_view, List::iterator> file_map;

    tl::expected<std::reference_wrapper<const File>, FileReadError> get_or_read(const std::string& uri);
    tl::expected<std::reference_wrapper<const File>, FileReadError> latest_file() const;
};
