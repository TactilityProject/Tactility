// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/file.h>

#include <stddef.h>
#include <sys/types.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/** Fixed size of an app instance's fd table. FD allocation uses the lowest unused index >= 3. */
#define APP_MAX_FDS 16

/**
 * FD-table dispatch for read()/write()/close(). When the calling task belongs to a running app
 * instance and @a fd is one that instance bound/allocated itself, it's looked up in that
 * instance's own fd table. Otherwise (no app instance, e.g. a kernel service task; or an app
 * instance's own fd that was never bound through app-module, e.g. a real file from fopen()/
 * open(), which this layer never intercepts) @a fd is a real underlying fd and the call falls
 * through to the real syscall unchanged.
 * @return number of bytes transferred, 0 on EOF (read) or a closed peer (write), or -1 with
 * errno set on failure. This is the fd-layer translation of AppFileOps::read()/write()'s ssize_t
 * and AppFileOps::close()'s error_t.
 */
ssize_t app_io_read(int fd, void* buffer, size_t size);
ssize_t app_io_write(int fd, const void* buffer, size_t size);
int app_io_close(int fd);

/**
 * FD-table dispatch for AppFileOps::await(): blocks until @a fd becomes readable/writable (per
 * @a wait) or @a timeout elapses, without transferring data - unlike app_io_read()/write(), which
 * always block indefinitely on a fd with no non-blocking mode of their own (see AppFileOps's own
 * doc). For a caller that needs to wait on its own stdio alongside something else on a timer,
 * e.g. relaying keystrokes to a child's stdin while also draining that child's stdout.
 * @retval ERROR_NOT_FOUND @a fd isn't bound in the calling task's own app instance fd table
 * @retval ERROR_TIMEOUT @a timeout elapsed
 * @retval ERROR_NONE the condition is true
 */
error_t app_io_await(int fd, AppFileWait wait, TickType_t timeout);

/**
 * Installs a custom AppFileOps at @a fd in the calling task's own app instance fd table,
 * replacing whatever was there (the null device, by default, for fd 0-2). Unlike a parent's
 * AppStreamBinding (see app_stream_subscribe()), writes through a self-bound fd are never teed
 * to the real underlying fd - the instance is taking full ownership of its own I/O (e.g. a
 * terminal app binding fd 0/1/2 to its own screen/keyboard), not being transparently observed
 * by something else that still expects to see the same output.
 * @warning Must be called from the app instance's own task.
 * @retval ERROR_NOT_FOUND the calling task isn't a running app instance
 * @retval ERROR_OUT_OF_RANGE @a fd is outside [0, APP_MAX_FDS)
 * @retval ERROR_NONE on success
 */
error_t app_io_bind_self(int fd, const struct AppFileOps* ops, void* object);

#ifdef __cplusplus
}
#endif
