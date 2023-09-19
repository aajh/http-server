#pragma once

#include <filesystem>
#include <vector>
#include <tl/expected.hpp>

const char FILE_FOLDER[] = "public";

struct File {
    std::vector<char> contents;
    std::filesystem::file_time_type last_write;
};

struct FileReadError {
    enum Type {
        INVALID_URI,
        NOT_FOUND,
        IO_ERROR,
    } type;

    std::error_code ec;
};
tl::expected<File, FileReadError> read_file_contents(const std::string& uri);
