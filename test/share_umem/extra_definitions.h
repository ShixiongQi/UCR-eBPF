#pragma once

#ifndef SO_PREFER_BUSY_POLL
#define SO_PREFER_BUSY_POLL	69
#endif

#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70
#endif

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

static void __exit_with_error(const char* error, const char* file, const char* func, int line) {
    fprintf(stderr, "error: %s \n  at %s %d %s\n", error, file, line, func);
    exit(EXIT_FAILURE);
}

#define exit_with_error(error)  __exit_with_error(error, __FILE__, __func__, __LINE__)


#define DEFAULT_SOCKET_PATH "/tmp/share_umem_manager.socket"
#define DEFAULT_SOCKET_MAX_BUFFER_SIZE 32

typedef __u64 u64;
typedef __u32 u32;
typedef __u16 u16;
typedef __u8  u8;

#define DEFAULT_FRAME_SIZE  4096
#define DEFAULT_NUM_FRAMES  (16*1024)
#define DEFAULT_CLIENT_FRAMES   1024
#define DEFAULT_UMEM_SIZE  (DEFAULT_NUM_FRAMES * DEFAULT_FRAME_SIZE)
#define DEFAULT_UMEM_FILE_NAME "share_umem"
#define DEFAULT_HEADROOM    0
#define DEFAULT_UMEM_FLAGS  0

#define REQUEST_ALLOC   1
#define REQUEST_DEALLOC 2
