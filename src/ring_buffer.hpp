#pragma once

#include "common.hpp"
#include <type_traits>
#include <tl/expected.hpp>

extern const size_t PAGE_SIZE;

struct RingBuffer {
    static constexpr size_t COPY_COUNT = 3;

    size_t length = 0;
    int memory_fd = -1;
    char* buffer = nullptr;

    private:
    RingBuffer() = default;
    public:
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&& o) : length(o.length), memory_fd(o.memory_fd), buffer(o.buffer) {
        o.memory_fd = -1;
        o.buffer = nullptr;
    }
    ~RingBuffer();

    template<typename T>
    char& operator[](T i) {
        assert(is_in_range(i));
        return buffer[i + length];
    }

    template<typename T>
    bool is_in_range(T i) {
        if constexpr (std::is_signed<T>::value) {
            return (i + length < COPY_COUNT*length) && (i + length > 0);
        } else {
            return i + length < COPY_COUNT*length;
        }
    }

    static tl::expected<RingBuffer, const char*> create(const size_t wanted_length = PAGE_SIZE);
};
