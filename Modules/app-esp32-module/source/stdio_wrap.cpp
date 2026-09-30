// SPDX-License-Identifier: Apache-2.0

// libc wraps (-Wl,--wrap=, see the top-level CMakeLists.txt) that route an app instance's calls to
// its own fds, cwd and exit(). App-instance behavior itself lives in app-module (app/libc.h).
#include <app/io.h>
#include <app/libc.h>
#include <app/scheduler.h>

#include <signal.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>

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

    int result;
    if (app_libc_try_window_size(fd, static_cast<unsigned long>(request), arg, &result)) {
        return result;
    }
    return __real_ioctl(fd, request, arg);
}

char* __real_getcwd(char* buf, size_t size);
int __real_chdir(const char* path);

char* __wrap_getcwd(char* buf, size_t size) {
    char* result;
    if (app_libc_try_getcwd(buf, size, &result)) {
        return result;
    }
    return __real_getcwd(buf, size);
}

int __wrap_chdir(const char* path) {
    int result;
    if (app_libc_try_chdir(path, &result)) {
        return result;
    }
    return __real_chdir(path);
}

// fstat() and isatty() both go through _fstat_r
int __real__fstat_r(struct _reent* r, int fd, struct stat* st);

int __wrap__fstat_r(struct _reent* r, int fd, struct stat* st) {
    int result;
    if (app_libc_try_fstat(fd, st, &result)) {
        return result;
    }
    return __real__fstat_r(r, fd, st);
}

int __real_poll(struct pollfd* fds, nfds_t nfds, int timeout);

int __wrap_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    int result;
    if (app_libc_try_poll(fds, nfds, timeout, __real_poll, &result)) {
        return result;
    }
    return __real_poll(fds, nfds, timeout);
}

int __real_tcgetattr(int fd, struct termios* p);
int __real_tcsetattr(int fd, int optional_actions, const struct termios* p);

int __wrap_tcgetattr(int fd, struct termios* p) {
    int result;
    if (app_libc_try_tcgetattr(fd, p, &result)) {
        return result;
    }
    return __real_tcgetattr(fd, p);
}

int __wrap_tcsetattr(int fd, int optional_actions, const struct termios* p) {
    int result;
    if (app_libc_try_tcsetattr(fd, p, &result)) {
        return result;
    }
    return __real_tcsetattr(fd, optional_actions, p);
}

// ESP-IDF's newlib has no signal() of its own, so this is the only definition
_sig_func_ptr signal(int sig, _sig_func_ptr handler) {
    AppLibcSignalHandler previous;
    if (app_libc_try_signal(sig, handler, &previous)) {
        return previous;
    }
    errno = ENOSYS;
    return SIG_ERR;
}

// Replaces the libc kill(), which reaches a _kill_r stub that aborts the device on some libc builds
int kill(pid_t pid, int sig) {
    int result;
    if (app_libc_try_kill(static_cast<int>(pid), sig, &result)) {
        return result;
    }
    errno = ENOSYS;
    return -1;
}

// Called by an app, newlib's exit() would reach _exit(), which aborts the whole device
[[noreturn]] void __real_exit(int status);

[[noreturn]] void __wrap_exit(int status) {
    app_scheduler_exit_current(status);
    __real_exit(status);
}

}
