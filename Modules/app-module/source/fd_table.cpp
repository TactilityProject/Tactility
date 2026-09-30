// SPDX-License-Identifier: Apache-2.0
#include <app/private/fd_table.h>
#include <app/private/null_device.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/error.h>

namespace {

bool fd_in_range(int fd) {
    return fd >= 0 && fd < APP_MAX_FDS;
}

bool get_internal(AppFdTable* table, int fd, AppFile* out_file, bool retain) {
    if (!fd_in_range(fd)) {
        return false;
    }

    mutex_lock(&table->mutex);
    check(!table->shutting_down);
    AppFdSlot& slot = table->slots[fd];
    bool in_use = slot.in_use;
    if (in_use) {
        *out_file = slot.file;
        if (retain && out_file->ops->retain != nullptr) {
            out_file->ops->retain(out_file->object);
        }
    }
    mutex_unlock(&table->mutex);
    return in_use;
}

} // namespace

extern "C" {

void app_fd_table_construct(AppFdTable* table) {
    const AppFile* null_file = app_null_file();
    for (int fd = 0; fd < APP_MAX_FDS; fd++) {
        table->slots[fd].in_use = false;
        table->slots[fd].ever_used = false;
    }
    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
        table->slots[fd].file = *null_file;
        table->slots[fd].in_use = true;
        table->slots[fd].ever_used = true;
    }
    table->shutting_down = false;
    mutex_construct(&table->mutex);
}

void app_fd_table_teardown(AppFdTable* table) {
    AppFile closing[APP_MAX_FDS];
    int closing_count = 0;

    mutex_lock(&table->mutex);
    table->shutting_down = true;
    for (int fd = 0; fd < APP_MAX_FDS; fd++) {
        if (table->slots[fd].in_use) {
            closing[closing_count++] = table->slots[fd].file;
            table->slots[fd].in_use = false;
        }
    }
    mutex_unlock(&table->mutex);

    for (int i = 0; i < closing_count; i++) {
        closing[i].ops->close(closing[i].object);
    }

    mutex_destruct(&table->mutex);
}

error_t app_fd_table_bind(AppFdTable* table, int fd, const AppFileOps* ops, void* object) {
    if (!fd_in_range(fd)) {
        return ERROR_OUT_OF_RANGE;
    }

    mutex_lock(&table->mutex);
    check(!table->shutting_down);
    AppFdSlot& slot = table->slots[fd];
    AppFile old = slot.file;
    bool had_old = slot.in_use;
    slot.file = { .ops = ops, .object = object };
    slot.in_use = true;
    slot.ever_used = true;
    mutex_unlock(&table->mutex);

    if (had_old) {
        old.ops->close(old.object);
    }
    return ERROR_NONE;
}

error_t app_fd_table_allocate(AppFdTable* table, const AppFileOps* ops, void* object, int* out_fd) {
    mutex_lock(&table->mutex);
    check(!table->shutting_down);
    for (int fd = 3; fd < APP_MAX_FDS; fd++) {
        AppFdSlot& slot = table->slots[fd];
        if (!slot.in_use) {
            slot.file = { .ops = ops, .object = object };
            slot.in_use = true;
            slot.ever_used = true;
            mutex_unlock(&table->mutex);
            *out_fd = fd;
            return ERROR_NONE;
        }
    }
    mutex_unlock(&table->mutex);
    return ERROR_RESOURCE;
}

bool app_fd_table_get(AppFdTable* table, int fd, AppFile* out_file) {
    return get_internal(table, fd, out_file, /*retain=*/false);
}

bool app_fd_table_get_and_retain(AppFdTable* table, int fd, AppFile* out_file) {
    return get_internal(table, fd, out_file, /*retain=*/true);
}

error_t app_fd_table_close(AppFdTable* table, int fd) {
    if (!fd_in_range(fd)) {
        return ERROR_NOT_FOUND;
    }

    mutex_lock(&table->mutex);
    check(!table->shutting_down);
    AppFdSlot& slot = table->slots[fd];
    bool in_use = slot.in_use;
    AppFile old = slot.file;
    slot.in_use = false;
    mutex_unlock(&table->mutex);

    if (!in_use) {
        return ERROR_NOT_FOUND;
    }
    return old.ops->close(old.object);
}

bool app_fd_table_is_app_owned(AppFdTable* table, int fd) {
    if (!fd_in_range(fd)) {
        return false;
    }

    mutex_lock(&table->mutex);
    bool owned = table->slots[fd].in_use || table->slots[fd].ever_used;
    mutex_unlock(&table->mutex);
    return owned;
}

} // extern "C"
