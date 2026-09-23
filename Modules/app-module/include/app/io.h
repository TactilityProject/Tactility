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
 * FD-table dispatch for read()/write()/close(). Falls through to the real syscall for a fd not
 * bound by the calling task's own app instance.
 */
ssize_t app_io_read(int fd, void* buffer, size_t size);
ssize_t app_io_write(int fd, const void* buffer, size_t size);
int app_io_close(int fd);

/**
 * FD-table dispatch for AppFileOps::await(): blocks until @a fd is ready or @a timeout elapses.
 * @retval ERROR_NOT_FOUND @a fd isn't bound in the calling task's own app instance fd table
 * @retval ERROR_TIMEOUT @a timeout elapsed
 * @retval ERROR_NONE the condition is true
 */
error_t app_io_await(int fd, AppFileWait wait, TickType_t timeout);

/**
 * Installs a custom AppFileOps at @a fd in the calling task's own app instance fd table.
 * Unlike AppStreamBinding, writes are never teed to the real underlying fd.
 * @warning Must be called from the app instance's own task.
 * @retval ERROR_NOT_FOUND the calling task isn't a running app instance
 * @retval ERROR_OUT_OF_RANGE @a fd is outside [0, APP_MAX_FDS)
 * @retval ERROR_NONE on success
 */
error_t app_io_bind_self(int fd, const struct AppFileOps* ops, void* object);

#ifdef __cplusplus
}
#endif
