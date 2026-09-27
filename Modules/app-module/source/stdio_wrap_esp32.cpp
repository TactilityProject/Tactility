// SPDX-License-Identifier: Apache-2.0
#ifdef ESP_PLATFORM

#include <app/private/stdio_wrap.h>

#include <app/io.h>

#include <cerrno>
#include <cstdarg>

// Newlib's own stdio calls the reentrant _read_r/_write_r/_close_r stubs directly, not the plain
// read/write/close wrappers, so those stubs are wrapped instead of the plain names. ioctl() has no
// such stub (esp_libc's vfs_calls.c defines the plain name directly), so it is wrapped as-is below.
#include <reent.h>

extern "C" {

ssize_t __wrap__read_r(struct _reent* r, int fd, void* buffer, size_t size) {
    (void)r;
    return app_io_read(fd, buffer, size);
}

ssize_t __wrap__write_r(struct _reent* r, int fd, const void* buffer, size_t size) {
    (void)r;
    return app_io_write(fd, buffer, size);
}

int __wrap__close_r(struct _reent* r, int fd) {
    (void)r;
    return app_io_close(fd);
}

int __real_ioctl(int fd, int request, void* arg);

int __wrap_ioctl(int fd, int request, ...) {
    va_list args;
    va_start(args, request);
    void* arg = va_arg(args, void*);
    va_end(args);

    struct winsize windowSize {};
    if (tryAppWindowSize(fd, static_cast<unsigned long>(request), arg, &windowSize)) {
        *static_cast<struct winsize*>(arg) = windowSize;
        return 0;
    }
    return __real_ioctl(fd, request, arg);
}

char* __real_getcwd(char* buf, size_t size);
int __real_chdir(const char* path);

char* __wrap_getcwd(char* buf, size_t size) {
    char* result;
    int err;
    if (tryAppGetCwd(buf, size, &result, &err)) {
        if (result == nullptr) {
            errno = err;
        }
        return result;
    }
    return __real_getcwd(buf, size);
}

int __wrap_chdir(const char* path) {
    int result;
    int err;
    if (tryAppChdir(path, &result, &err)) {
        if (result != 0) {
            errno = err;
        }
        return result;
    }
    return __real_chdir(path);
}

// fstat() and isatty() both go through _fstat_r
int __real__fstat_r(struct _reent* r, int fd, struct stat* st);

int __wrap__fstat_r(struct _reent* r, int fd, struct stat* st) {
    int result;
    if (tryAppFstat(fd, st, &result)) {
        return result;
    }
    return __real__fstat_r(r, fd, st);
}

int __real_poll(struct pollfd* fds, nfds_t nfds, int timeout);

int __wrap_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    int result;
    if (tryAppPoll(fds, nfds, timeout, __real_poll, &result)) {
        return result;
    }
    return __real_poll(fds, nfds, timeout);
}

int __real_tcgetattr(int fd, struct termios* p);
int __real_tcsetattr(int fd, int optional_actions, const struct termios* p);

int __wrap_tcgetattr(int fd, struct termios* p) {
    int result;
    if (tryAppTcgetattr(fd, p, &result)) {
        return result;
    }
    return __real_tcgetattr(fd, p);
}

int __wrap_tcsetattr(int fd, int optional_actions, const struct termios* p) {
    int result;
    if (tryAppTcsetattr(fd, &result)) {
        return result;
    }
    return __real_tcsetattr(fd, optional_actions, p);
}

}

#endif // ESP_PLATFORM
