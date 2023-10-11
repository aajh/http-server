#include "file.hpp"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <cstdio>

static const std::unordered_map<std::string_view, const char*> MIME_TYPES = {
    { "txt", "text/plain" },
    { "html", "text/html" },
    { "htm", "text/html" },
    { "js", "text/javacsript" },
    { "css", "text/css" },
    { "json", "application/json" },
    { "jpeg", "image/jpeg" },
    { "jpg", "image/jpeg" },
    { "png", "image/png" },
    { "svg", "image/svg+xml" },
    { "webp", "image/webp", },
    { "avif", "image/avif" },
};

static std::string_view get_mime_type(const std::filesystem::path& path) {
    const auto extension_path = path.extension();
    std::string_view extension = extension_path.native();
    if (extension.size() == 0) return DEFAULT_MIME_TYPE;
    if (extension[0] == '.') extension = extension.substr(1);

    auto search = MIME_TYPES.find(extension);
    return search != MIME_TYPES.end() ? search->second : DEFAULT_MIME_TYPE;
}

tl::expected<File, FileReadError> read_file_contents(const std::filesystem::path& path) {
    std::error_code ec;

    auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec });
    }
    if (!exists) {
        return tl::unexpected(FileReadError{ FileReadError::NOT_FOUND, ec });
    }

    File ret;

    ret.mime_type = get_mime_type(path);

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

FileCache::FileCache(const char* folder) {
    file_root_path = (std::filesystem::current_path() / folder).lexically_normal();
}

tl::expected<std::reference_wrapper<const File>, FileReadError> FileCache::get_or_read(const std::string& uri_path) {
    auto path = get_filesystem_path_from_uri_path(uri_path);
    if (!path) {
        return tl::unexpected(path.error());
    }

    if (auto search = file_map.find(*path); search != file_map.end()) {
        const auto& it = search->second;
        const auto now = Clock::now();

        if (now - it->last_accessed > MAX_ENTRY_LIFETIME) {
            cache_size -= it->file.contents.size();
            file_list.erase(it);
            file_map.erase(search);

            goto read_file;
        }

        it->last_accessed = now;

        if (it != file_list.begin()) {
            file_list.push_front(std::move(*it));
            file_list.erase(it);
            auto begin = file_list.begin();
            file_map[begin->path] = begin;
        }

        return latest_file();
    }

read_file:
    auto read_file = read_file_contents(*path);
    if (!read_file && read_file.error().type == FileReadError::IO_ERROR) {
        return tl::unexpected(read_file.error());
    }

    if (read_file && read_file->contents.size() > MAX_CACHED_FILE_SIZE) {
        return *read_file;
    }

    Entry new_entry;
    new_entry.path = *path;
    if (read_file) {
        new_entry.file = std::move(*read_file);
    } else {
        new_entry.status = read_file.error().type;
    }
    new_entry.last_accessed = Clock::now();

    file_list.push_front(std::move(new_entry));
    auto it = file_list.begin();
    file_map[it->path] = it;

    cache_size += it->file.contents.size();
    trim();

    return latest_file();
}

tl::expected<std::filesystem::path, FileReadError> FileCache::get_filesystem_path_from_uri_path(const std::string& uri_path) const {
    std::error_code ec;

    if (uri_path.size() == 0 || uri_path[0] != '/') {
        return tl::unexpected(FileReadError{ FileReadError::INVALID_URI, ec });
    }

    auto path = std::filesystem::weakly_canonical(file_root_path / uri_path.substr(1), ec);
    if (ec) {
        return tl::unexpected(FileReadError{ FileReadError::IO_ERROR, ec });
    }

    bool is_inside_root_path = true;
    for (auto r = file_root_path.begin(), p = path.begin(); r != file_root_path.end() && *r != ""; ++r, ++p) {
        if (p == path.end() || *p != *r) {
            is_inside_root_path = false;
            break;
        }
    }
    if (!is_inside_root_path) {
        return tl::unexpected(FileReadError{ FileReadError::INVALID_URI, ec });
    }

    return path;
}


tl::expected<std::reference_wrapper<const File>, FileReadError> FileCache::latest_file() const {
    assert(!file_list.empty());
    auto& entry = file_list.front();
    if (entry.status) {
        return tl::unexpected(FileReadError{ entry.status });
    } else {
        return entry.file;
    }
}

void FileCache::trim() {
    while (cache_size > MAX_CACHE_SIZE || file_list.size() > MAX_CACHE_ENTRIES) {
        const auto& entry = file_list.back();
        cache_size -= entry.file.contents.size();
        file_map.erase(entry.path);
        file_list.pop_back();
    }
}
