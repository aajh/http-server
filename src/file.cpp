#include "file.hpp"

#include <algorithm>
#include <filesystem>
#include <stdio.h>
#include <string.h>

static const std::filesystem::path FILE_SERVE_ROOT = (std::filesystem::current_path() / FILE_FOLDER).lexically_normal();

tl::expected<File, FileReadError> read_file_contents(const std::string& uri) {
    std::error_code ec;

    if (uri.size() == 0 || uri[0] != '/') {
        return tl::unexpected(FileReadError{ FileReadError::INVALID_URI, ec });
    }

    auto path = std::filesystem::weakly_canonical(FILE_SERVE_ROOT / uri.substr(1), ec);
    if (ec) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec });
    }

    auto first_mismatch = std::mismatch(FILE_SERVE_ROOT.begin(), FILE_SERVE_ROOT.end(), path.begin()).first;
    if (first_mismatch != FILE_SERVE_ROOT.end()) {
        return tl::unexpected(FileReadError{ FileReadError::INVALID_URI, ec });
    }

    auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec });
    }
    if (!exists) {
        return tl::unexpected(FileReadError{ FileReadError::NOT_FOUND, ec });
    }

    File ret;

    ret.last_write = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec });
    }

    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec, strerror(errno) });
    }

    if (fseek(file, 0, SEEK_END)) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec, strerror(errno) });
    }
    const auto ftell_result = ftell(file);
    if (ftell_result < 0) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec, strerror(errno) });
    }
    const size_t length = ftell_result;
    if (fseek(file, 0, SEEK_SET)) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec, strerror(errno) });
    }

    ret.contents.resize(length);
    const auto read_bytes = fread(ret.contents.data(), 1, length, file);
    if (read_bytes != length) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR });
    }

    return ret;
}
