#include "ring_buffer.hpp"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static int shm_open_anon();

const size_t PAGE_SIZE = getpagesize();

RingBuffer::~RingBuffer() {
    if (buffer) {
        munmap(buffer, COPY_COUNT*length);
    }
    if (memory_fd != -1) {
        close(memory_fd);
    }
}

tl::expected<RingBuffer, const char*> RingBuffer::create(const size_t wanted_length) {
    RingBuffer ret;

    size_t page_count = wanted_length / PAGE_SIZE;
    if (page_count*PAGE_SIZE < wanted_length) {
        page_count += 1;
    }
    ret.length = page_count*PAGE_SIZE;

    ret.memory_fd = shm_open_anon();
    if (ret.memory_fd == -1) {
        return tl::unexpected(strerror(errno));
    }
    if (ftruncate(ret.memory_fd, ret.length)) {
        return tl::unexpected(strerror(errno));
    }

    ret.buffer = (char*)mmap(NULL, COPY_COUNT*ret.length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret.buffer == MAP_FAILED) {
        ret.buffer = nullptr;
        return tl::unexpected(strerror(errno));
    }

    for (size_t i = 0; i < COPY_COUNT; ++i) {
        auto result = mmap(ret.buffer + i*ret.length, ret.length, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, ret.memory_fd, 0);
        if (result == MAP_FAILED) {
            return tl::unexpected(strerror(errno));
        }
    }

    return ret;
}


// https://github.com/lassik/shm_open_anon/blob/master/shm_open_anon.c
//
// Copyright 2019 Lassi Kortela
// 
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
// 
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

static int
shm_unlink_or_close(const char *name, int fd)
{
    int save;

    if (shm_unlink(name) == -1) {
        save = errno;
        close(fd);
        errno = save;
        return -1;
    }
    return fd;
}

static int
shm_open_anon(void)
{
	char name[16] = "/shm-";
	struct timespec tv;
	unsigned long r;
	char *const limit = name + sizeof(name) - 1;
	char *start;
	char *fill;
	int fd, tries;

	*limit = 0;
	start = name + strlen(name);
	for (tries = 0; tries < 4; tries++) {
		clock_gettime(CLOCK_REALTIME, &tv);
		r = (unsigned long)tv.tv_sec + (unsigned long)tv.tv_nsec;
		for (fill = start; fill < limit; r /= 8)
			*fill++ = '0' + (r % 8);
		fd = shm_open(
		  name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (fd != -1)
			return shm_unlink_or_close(name, fd);
		if (errno != EEXIST)
			break;
	}
	return -1;
}

