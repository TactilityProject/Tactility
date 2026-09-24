// SPDX-License-Identifier: Apache-2.0
#include <app/io.h>

#include <app/private/fd_table.h>
#include <app/private/ledger.h>
#include <app/scheduler.h>

#include <cerrno>

#if defined(TT_APP_IO_WRAPS_STDIO)
#ifdef ESP_PLATFORM
// Newlib's stdio (fflush()'s buffer-flush path, in particular) calls the reentrant _read_r/
// _write_r/_close_r syscall stubs directly. The plain read()/write()/close() newlib provides are
// just thin wrappers around them (esp-idf's components/newlib/src/syscalls.c: `write(fd, dst,
// size) { return _write_r(__getreent(), fd, dst, size); }`). Wrapping the _r stubs catches both;
// wrapping the plain names would only catch direct write()-style callers.
#include <reent.h>
extern "C" {
ssize_t __real__read_r(struct _reent* r, int fd, void* buffer, size_t size);
ssize_t __real__write_r(struct _reent* r, int fd, const void* buffer, size_t size);
int __real__close_r(struct _reent* r, int fd);
}
namespace {
ssize_t real_read(int fd, void* buffer, size_t size) { return __real__read_r(__getreent(), fd, buffer, size); }
ssize_t real_write(int fd, const void* buffer, size_t size) { return __real__write_r(__getreent(), fd, buffer, size); }
int real_close(int fd) { return __real__close_r(__getreent(), fd); }
} // namespace
#else
extern "C" {
ssize_t __real_read(int fd, void* buffer, size_t size);
ssize_t __real_write(int fd, const void* buffer, size_t size);
int __real_close(int fd);
}
namespace {
ssize_t real_read(int fd, void* buffer, size_t size) { return __real_read(fd, buffer, size); }
ssize_t real_write(int fd, const void* buffer, size_t size) { return __real_write(fd, buffer, size); }
int real_close(int fd) { return __real_close(fd); }
} // namespace
#endif
#else
#include <unistd.h>
namespace {
ssize_t real_read(int fd, void* buffer, size_t size) { return ::read(fd, buffer, size); }
ssize_t real_write(int fd, const void* buffer, size_t size) { return ::write(fd, buffer, size); }
int real_close(int fd) { return ::close(fd); }
} // namespace
#endif

namespace {

// NULL for a caller not running as an app instance (e.g. a kernel service task). @a fd is then
// a real underlying fd, handled by the caller falling through to the real syscall.
AppFdTable* current_app_fd_table() {
    AppInstanceId app_id = app_scheduler_current_app_id();
    if (app_id == 0) {
        return nullptr;
    }
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_id);
    AppFdTable* table = (iterator != ledger.instances.end()) ? &iterator->second.fd_table : nullptr;
    mutex_unlock(&ledger.mutex);
    return table;
}

} // namespace

extern "C" {

ssize_t app_io_read(int fd, void* buffer, size_t size) {
    AppFdTable* table = current_app_fd_table();
    AppFile file {};
    if (table != nullptr && app_fd_table_get_and_retain(table, fd, &file)) {
        ssize_t result = file.ops->read(file.object, buffer, size);
        if (file.ops->release != nullptr) {
            file.ops->release(file.object);
        }
        return result;
    }
    // @a fd isn't currently bound. If this table has never touched it either, it's a real
    // underlying fd (e.g. from fopen()/open(), which app-module never intercepts; see app/io.h),
    // so fall through. Otherwise it's one of ours that's already been closed: a real EBADF, not
    // a real fd to hand to the platform (which could belong to something else entirely by now).
    if (table != nullptr && app_fd_table_is_app_owned(table, fd)) {
        errno = EBADF;
        return -1;
    }
    return real_read(fd, buffer, size);
}

ssize_t app_io_write(int fd, const void* buffer, size_t size) {
    AppFdTable* table = current_app_fd_table();
    AppFile file {};
    if (table != nullptr && app_fd_table_get_and_retain(table, fd, &file)) {
        ssize_t result = file.ops->write(file.object, buffer, size);
        if (file.ops->release != nullptr) {
            file.ops->release(file.object);
        }
        // Tee to the real fd too: a bound stream normally exists because a parent explicitly
        // asked to capture this app instance's own output (see AppStreamBinding), but generic
        // code running on that same instance's thread - most commonly the platform's own logging
        // (LOG_I/etc, which calls write() the same as anything else) - has no way to know its
        // output is currently being intercepted. Without this, a log line emitted while any app
        // instance has its stdout captured would vanish from the console entirely instead of
        // just also being visible to the capturing parent. An instance that bound its own fd
        // (see app_io_bind_self()) opts out - it wants exclusive ownership, not a silent tap.
        if (!file.suppress_console_tee) {
            real_write(fd, buffer, size);
        }
        return result;
    }
    if (table != nullptr && app_fd_table_is_app_owned(table, fd)) {
        errno = EBADF;
        return -1;
    }
    return real_write(fd, buffer, size);
}

int app_io_close(int fd) {
    AppFdTable* table = current_app_fd_table();
    if (table != nullptr) {
        error_t result = app_fd_table_close(table, fd);
        if (result == ERROR_NONE) {
            return 0;
        }
        if (app_fd_table_is_app_owned(table, fd)) {
            errno = EBADF;
            return -1;
        }
    }
    return real_close(fd);
}

error_t app_io_await(int fd, AppFileWait wait, TickType_t timeout) {
    AppFdTable* table = current_app_fd_table();
    if (table == nullptr) {
        return ERROR_NOT_FOUND;
    }
    AppFile file {};
    if (!app_fd_table_get_and_retain(table, fd, &file)) {
        return ERROR_NOT_FOUND;
    }
    error_t result = file.ops->await(file.object, wait, timeout);
    if (file.ops->release != nullptr) {
        file.ops->release(file.object);
    }
    return result;
}

error_t app_io_ioctl(int fd, AppIoctlRequest request, void* arg) {
    AppFdTable* table = current_app_fd_table();
    if (table == nullptr) {
        return ERROR_NOT_FOUND;
    }
    AppFile file {};
    if (!app_fd_table_get_and_retain(table, fd, &file)) {
        return ERROR_NOT_FOUND;
    }
    error_t result = (file.ops->ioctl != nullptr) ? file.ops->ioctl(file.object, request, arg) : ERROR_NOT_SUPPORTED;
    if (file.ops->release != nullptr) {
        file.ops->release(file.object);
    }
    return result;
}

error_t app_io_bind_self(int fd, const AppFileOps* ops, void* object) {
    AppFdTable* table = current_app_fd_table();
    if (table == nullptr) {
        return ERROR_NOT_FOUND;
    }
    return app_fd_table_bind(table, fd, ops, object, /*suppress_console_tee=*/true);
}

} // extern "C"
