// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <tactility/error.h>
#include <tactility/freertos/freertos.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which readiness condition to block for in AppFileOps::await(). */
typedef enum {
    APP_FILE_WAIT_READABLE,
    APP_FILE_WAIT_WRITABLE,
} AppFileWait;

/** Bits returned by AppFileOps::poll(). */
#define APP_FILE_READABLE (1u << 0)
#define APP_FILE_WRITABLE (1u << 1)

/**
 * Operations table for one kind of file-like object (stream, file, device, ...). Every function
 * takes the type-erased `object` a concrete AppFile instance was constructed with.
 */
struct AppFileOps {
    ssize_t (*read)(void* object, void* buffer, size_t size);
    ssize_t (*write)(void* object, const void* buffer, size_t size);
    error_t (*close)(void* object);
    error_t (*await)(void* object, AppFileWait wait, TickType_t timeout);
    /** @return bitmask of APP_FILE_READABLE / APP_FILE_WRITABLE. */
    uint32_t (*poll)(void* object);
    /** Optional (may be NULL). Called by app_fd_table_get_and_retain() atomically with the fd
     * lookup, before handing `object` to a caller about to dispatch into it, so a concurrent
     * teardown of `object` can wait for that dispatch to finish instead of racing it. The caller
     * calls release() once done, regardless of what the dispatched call returned. */
    void (*retain)(void* object);
    void (*release)(void* object);
};

/** A file-descriptor-table entry: an operations table paired with the object it operates on. */
struct AppFile {
    const struct AppFileOps* ops;
    void* object;
    /** If true, app_io_write() skips its usual tee to the real underlying fd. Set by
     * app_io_bind_self() - an instance replacing its own I/O (e.g. a terminal app owning its
     * fd 0/1/2) wants exclusive ownership, unlike a parent's AppStreamBinding, which observes
     * output that should still also reach the real console. */
    bool suppress_console_tee;
};

#ifdef __cplusplus
}
#endif
