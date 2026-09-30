// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/file.h>
#include <app/io.h>

#include <stdbool.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/error.h>

/**
 * One fd's worth of state in an AppFdTable. `file` is valid only while `in_use` is true.
 * `ever_used` is set once `in_use` is ever set true (including the default stdio binding at
 * construct time) and never cleared, even once `in_use` goes back to false on close. This is
 * what lets app_fd_table_is_app_owned() distinguish "this fd number belongs to us, it's just
 * currently closed" (an app-level EBADF) from "app-module has never touched this fd number" (a
 * real underlying fd, e.g. from fopen()/open(), that a caller should fall through on).
 *
 * @warning This is internal data. Do not read/write to it directly.
 */
struct AppFdSlot {
    struct AppFile file;
    bool in_use;
    bool ever_used;
};

/**
 * Every app instance's fd table (see AppInstanceRecord). `mutex` guards `slots` only; it is
 * never held while calling into an AppFile's ops (dispatch copies the AppFile out under the lock
 * first), since those calls belong to independently synchronized objects (AppStream's own
 * mutex/event group) and may run concurrently with a close()/bind() replacing an unrelated slot.
 *
 * `shutting_down` is set under `mutex` by app_fd_table_teardown() before it destructs `mutex`,
 * and checked (also under `mutex`) by every other entry point. Callers are expected to
 * serialize against teardown externally (see app_stream_subscribe()/app_stream_unsubscribe()),
 * so a check() failure here means that external synchronization broke, not a condition to
 * handle gracefully.
 *
 * @warning This is internal data. Do not read/write to it directly.
 */
struct AppFdTable {
    struct AppFdSlot slots[APP_MAX_FDS];
    struct Mutex mutex;
    bool shutting_down;
};

#ifdef __cplusplus
extern "C" {
#endif

/** Initializes every fd to the null device (see app/private/null_device.h) and constructs `mutex`. */
void app_fd_table_construct(struct AppFdTable* table);

/** Closes every still-open fd (via AppFileOps::close()) and destructs `mutex`. */
void app_fd_table_teardown(struct AppFdTable* table);

/**
 * Installs {ops, object} at @a fd, closing whatever was previously there first (the null device
 * counts as "previously there" for fds 0-2, so this doubles as their initial stdio binding).
 * @retval ERROR_OUT_OF_RANGE @a fd is outside [0, APP_MAX_FDS)
 */
error_t app_fd_table_bind(struct AppFdTable* table, int fd, const struct AppFileOps* ops, void* object);

/**
 * Installs {ops, object} at the lowest unused fd >= 3.
 * @retval ERROR_RESOURCE the table is full
 */
error_t app_fd_table_allocate(struct AppFdTable* table, const struct AppFileOps* ops, void* object, int* out_fd);

/** @return true and fills @a out_file if @a fd is currently in use, false otherwise. */
bool app_fd_table_get(struct AppFdTable* table, int fd, struct AppFile* out_file);

/**
 * Like app_fd_table_get(), but also calls the returned AppFile's AppFileOps::retain() (if any)
 * while still holding `mutex`, atomically with the lookup. A caller that gets true back can
 * therefore never have @a fd closed/torn down out from under it between this call and actually
 * dispatching into the returned AppFile. Caller must call AppFileOps::release() once done
 * dispatching.
 */
bool app_fd_table_get_and_retain(struct AppFdTable* table, int fd, struct AppFile* out_file);

/**
 * Closes @a fd (via AppFileOps::close()) and frees the slot for reuse.
 * @retval ERROR_NOT_FOUND @a fd is out of range or not currently in use
 */
error_t app_fd_table_close(struct AppFdTable* table, int fd);

/**
 * @return true if @a fd is currently in use, or was in the past (even if since closed). This is
 * whether the table claims @a fd as one of its own, regardless of its current state. False means
 * @a fd is a real underlying fd this table has never bound or allocated.
 */
bool app_fd_table_is_app_owned(struct AppFdTable* table, int fd);

#ifdef __cplusplus
}
#endif
