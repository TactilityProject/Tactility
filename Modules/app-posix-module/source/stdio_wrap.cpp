// SPDX-License-Identifier: Apache-2.0

// libc wraps that route an app instance's calls to its own fds, cwd and exit(). App-instance behavior
// itself lives in app-module (app/libc.h). POSIX can't use -Wl,--wrap= like ESP32: --wrap doesn't reach
// a dlopen()ed app's own printf/write calls, so these wraps are installed under their real names instead
// - dyld interpose on Apple (stdio_wrap_apple.cpp), plain strong definitions elsewhere (stdio_wrap_elf.cpp).
// app-module's io.cpp falls through to the __real_read/__real_write/__real_close defined here.
#include <app_posix/stdio_wrap.h>

#include <app/io.h>
#include <app/libc.h>
#include <app/scheduler.h>
#include <app/signal.h>

#include <tactility/paths.h>

#include <cerrno>
#include <cstring>

// region syscall wraps

// dlsym(RTLD_NEXT, ...) avoids recursing into the overrides installed under the real names.
#include <dlfcn.h>
#include <memory>
#include <unistd.h>

extern "C" {

ssize_t __real_read(int fd, void* buffer, size_t size) {
    static auto real = reinterpret_cast<ssize_t (*)(int, void*, size_t)>(dlsym(RTLD_NEXT, "read"));
    return real(fd, buffer, size);
}

ssize_t __real_write(int fd, const void* buffer, size_t size) {
    static auto real = reinterpret_cast<ssize_t (*)(int, const void*, size_t)>(dlsym(RTLD_NEXT, "write"));
    return real(fd, buffer, size);
}

int __real_close(int fd) {
    static auto real = reinterpret_cast<int (*)(int)>(dlsym(RTLD_NEXT, "close"));
    return real(fd);
}

int __real_ioctl(int fd, unsigned long request, void* arg) {
    static auto real = reinterpret_cast<int (*)(int, unsigned long, void*)>(dlsym(RTLD_NEXT, "ioctl"));
    return real(fd, request, arg);
}

char* __real_getcwd(char* buf, size_t size) {
    static auto real = reinterpret_cast<char* (*)(char*, size_t)>(dlsym(RTLD_NEXT, "getcwd"));
    return real(buf, size);
}

int __real_chdir(const char* path) {
    static auto real = reinterpret_cast<int (*)(const char*)>(dlsym(RTLD_NEXT, "chdir"));
    return real(path);
}

int __real_fstat(int fd, struct stat* st) {
    static auto real = reinterpret_cast<int (*)(int, struct stat*)>(dlsym(RTLD_NEXT, "fstat"));
    return real(fd, st);
}

int __real_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    static auto real = reinterpret_cast<int (*)(struct pollfd*, nfds_t, int)>(dlsym(RTLD_NEXT, "poll"));
    return real(fds, nfds, timeout);
}

int __real_tcgetattr(int fd, struct termios* p) {
    static auto real = reinterpret_cast<int (*)(int, struct termios*)>(dlsym(RTLD_NEXT, "tcgetattr"));
    return real(fd, p);
}

int __real_tcsetattr(int fd, int optional_actions, const struct termios* p) {
    static auto real = reinterpret_cast<int (*)(int, int, const struct termios*)>(dlsym(RTLD_NEXT, "tcsetattr"));
    return real(fd, optional_actions, p);
}

AppLibcSignalHandler __real_signal(int sig, AppLibcSignalHandler handler) {
    static auto real = reinterpret_cast<AppLibcSignalHandler (*)(int, AppLibcSignalHandler)>(dlsym(RTLD_NEXT, "signal"));
    return real(sig, handler);
}

int __real_kill(pid_t pid, int sig) {
    static auto real = reinterpret_cast<int (*)(pid_t, int)>(dlsym(RTLD_NEXT, "kill"));
    return real(pid, sig);
}

pid_t __real_getpid() {
    static auto real = reinterpret_cast<pid_t (*)()>(dlsym(RTLD_NEXT, "getpid"));
    return real();
}

pid_t __real_getppid() {
    static auto real = reinterpret_cast<pid_t (*)()>(dlsym(RTLD_NEXT, "getppid"));
    return real();
}

int __real_usleep(useconds_t usec) {
    static auto real = reinterpret_cast<int (*)(useconds_t)>(dlsym(RTLD_NEXT, "usleep"));
    return real(usec);
}

unsigned int __real_sleep(unsigned int seconds) {
    static auto real = reinterpret_cast<unsigned int (*)(unsigned int)>(dlsym(RTLD_NEXT, "sleep"));
    return real(seconds);
}

[[noreturn]] void __real_exit(int status) {
    static auto real = reinterpret_cast<void (*)(int)>(dlsym(RTLD_NEXT, "exit"));
    real(status);
    __builtin_unreachable();
}

// Pending signals are delivered at the entry of these wraps, and again when a call was interrupted
// by one, since the app holds no lock of the system itself there.

ssize_t __wrap_read(int fd, void* buffer, size_t size) {
    app_signal_deliver_pending();
    const ssize_t result = app_io_read(fd, buffer, size);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

ssize_t __wrap_write(int fd, const void* buffer, size_t size) {
    app_signal_deliver_pending();
    const ssize_t result = app_io_write(fd, buffer, size);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

int __wrap_close(int fd) {
    return app_io_close(fd);
}

int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void* arg = va_arg(args, void*);
    va_end(args);

    int result;
    if (app_libc_try_window_size(fd, request, arg, &result)) {
        return result;
    }
    return __real_ioctl(fd, request, arg);
}

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

int __wrap_fstat(int fd, struct stat* st) {
    int result;
    if (app_libc_try_fstat(fd, st, &result)) {
        return result;
    }
    return __real_fstat(fd, st);
}

int __wrap_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    app_signal_deliver_pending();
    int result;
    if (app_libc_try_poll(fds, nfds, timeout, __real_poll, &result)) {
        if (result < 0 && errno == EINTR) {
            app_signal_deliver_pending();
        }
        return result;
    }
    return __real_poll(fds, nfds, timeout);
}

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

// Called by an app, the real signal() would install a process-wide handler pointing into the app's binary
AppLibcSignalHandler __wrap_signal(int sig, AppLibcSignalHandler handler) {
    AppLibcSignalHandler previous;
    if (app_libc_try_signal(sig, handler, &previous)) {
        return previous;
    }
    return __real_signal(sig, handler);
}

// Called by an app, the real kill() would signal the whole simulator (e.g. SIGSTOP)
int __wrap_kill(pid_t pid, int sig) {
    int result;
    if (app_libc_try_kill(pid, sig, &result)) {
        // A signal sent to the app itself is delivered before kill() returns
        app_signal_deliver_pending();
        return result;
    }
    return __real_kill(pid, sig);
}

pid_t __wrap_getpid() {
    int result;
    if (app_libc_try_getpid(&result)) {
        return result;
    }
    return __real_getpid();
}

pid_t __wrap_getppid() {
    int result;
    if (app_libc_try_getppid(&result)) {
        return result;
    }
    return __real_getppid();
}

int __wrap_usleep(useconds_t usec) {
    app_signal_deliver_pending();
    int result;
    if (app_libc_try_usleep(usec, &result)) {
        app_signal_deliver_pending();
        return result;
    }
    return __real_usleep(usec);
}

unsigned int __wrap_sleep(unsigned int seconds) {
    app_signal_deliver_pending();
    unsigned int result;
    if (app_libc_try_sleep(seconds, &result)) {
        app_signal_deliver_pending();
        return result;
    }
    return __real_sleep(seconds);
}

// Called by an app, the real exit() would end the whole simulator
void __wrap_exit(int status) {
    app_scheduler_exit_current(status);
    __real_exit(status);
}

}

// endregion

// region path wraps
//
// The process has a single cwd, so an app instance's relative paths are resolved against its own
// first (see app/libc.h). Every other caller's paths are passed on as-is.

namespace {

/**
 * @return the path to pass on: resolved for an app instance, as-is otherwise.
 * NULL when the resolved path doesn't fit (errno is set to ENAMETOOLONG).
 */
const char* resolvePath(const char* path, char (&buffer)[FILE_MAX_PATH_STRING_LENGTH]) {
    const char* resolved;
    return app_libc_try_resolve_path(path, buffer, sizeof(buffer), &resolved) ? resolved : path;
}

/** open() only reads its mode argument when it can create a file */
bool openNeedsMode(int flags) {
#ifdef O_TMPFILE
    if ((flags & O_TMPFILE) == O_TMPFILE) {
        return true;
    }
#endif
    return (flags & O_CREAT) != 0;
}

} // namespace

extern "C" {

int __real_open(const char* path, int flags, mode_t mode) {
    static auto real = reinterpret_cast<int (*)(const char*, int, ...)>(dlsym(RTLD_NEXT, "open"));
    return real(path, flags, mode);
}

FILE* __real_fopen(const char* path, const char* mode) {
    static auto real = reinterpret_cast<FILE* (*)(const char*, const char*)>(dlsym(RTLD_NEXT, "fopen"));
    return real(path, mode);
}

int __real_stat(const char* path, struct stat* st) {
    static auto real = reinterpret_cast<int (*)(const char*, struct stat*)>(dlsym(RTLD_NEXT, "stat"));
    return real(path, st);
}

int __real_lstat(const char* path, struct stat* st) {
    static auto real = reinterpret_cast<int (*)(const char*, struct stat*)>(dlsym(RTLD_NEXT, "lstat"));
    return real(path, st);
}

int __real_access(const char* path, int mode) {
    static auto real = reinterpret_cast<int (*)(const char*, int)>(dlsym(RTLD_NEXT, "access"));
    return real(path, mode);
}

int __real_unlink(const char* path) {
    static auto real = reinterpret_cast<int (*)(const char*)>(dlsym(RTLD_NEXT, "unlink"));
    return real(path);
}

int __real_remove(const char* path) {
    static auto real = reinterpret_cast<int (*)(const char*)>(dlsym(RTLD_NEXT, "remove"));
    return real(path);
}

int __real_rename(const char* src, const char* dst) {
    static auto real = reinterpret_cast<int (*)(const char*, const char*)>(dlsym(RTLD_NEXT, "rename"));
    return real(src, dst);
}

int __real_mkdir(const char* path, mode_t mode) {
    static auto real = reinterpret_cast<int (*)(const char*, mode_t)>(dlsym(RTLD_NEXT, "mkdir"));
    return real(path, mode);
}

int __real_rmdir(const char* path) {
    static auto real = reinterpret_cast<int (*)(const char*)>(dlsym(RTLD_NEXT, "rmdir"));
    return real(path);
}

DIR* __real_opendir(const char* path) {
    static auto real = reinterpret_cast<DIR* (*)(const char*)>(dlsym(RTLD_NEXT, "opendir"));
    return real(path);
}

int __real_fclose(FILE* file) {
    static auto real = reinterpret_cast<int (*)(FILE*)>(dlsym(RTLD_NEXT, "fclose"));
    return real(file);
}

FILE* __real_fdopen(int fd, const char* mode) {
    static auto real = reinterpret_cast<FILE* (*)(int, const char*)>(dlsym(RTLD_NEXT, "fdopen"));
    return real(fd, mode);
}

int __real_closedir(DIR* dir) {
    static auto real = reinterpret_cast<int (*)(DIR*)>(dlsym(RTLD_NEXT, "closedir"));
    return real(dir);
}

int __real_truncate(const char* path, off_t length) {
    static auto real = reinterpret_cast<int (*)(const char*, off_t)>(dlsym(RTLD_NEXT, "truncate"));
    return real(path, length);
}

int __wrap_open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (openNeedsMode(flags)) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_open(resolved, flags, mode);
}

// libc's own fopen() calls an internal open() alias, which the open() wrap can't reach
FILE* __wrap_fopen(const char* path, const char* mode) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return nullptr;
    }
    return __real_fopen(resolved, mode);
}

int __wrap_stat(const char* path, struct stat* st) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_stat(resolved, st);
}

int __wrap_lstat(const char* path, struct stat* st) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_lstat(resolved, st);
}

int __wrap_access(const char* path, int mode) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_access(resolved, mode);
}

int __wrap_unlink(const char* path) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_unlink(resolved);
}

int __wrap_remove(const char* path) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_remove(resolved);
}

int __wrap_rename(const char* src, const char* dst) {
    char src_buffer[FILE_MAX_PATH_STRING_LENGTH];
    char dst_buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved_src = resolvePath(src, src_buffer);
    const char* resolved_dst = resolvePath(dst, dst_buffer);
    if (resolved_src == nullptr || resolved_dst == nullptr) {
        return -1;
    }
    return __real_rename(resolved_src, resolved_dst);
}

int __wrap_mkdir(const char* path, mode_t mode) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_mkdir(resolved, mode);
}

int __wrap_rmdir(const char* path) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_rmdir(resolved);
}

DIR* __wrap_opendir(const char* path) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return nullptr;
    }
    return __real_opendir(resolved);
}

int __wrap_truncate(const char* path, off_t length) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved = resolvePath(path, buffer);
    if (resolved == nullptr) {
        return -1;
    }
    return __real_truncate(resolved, length);
}

}

// endregion

// region stdio wraps
//
// libc's printf/fprintf/etc call an internal, non-exported write() alias that the read/write/close
// wraps above can't reach, so these redirect calls to printf/fprintf/etc directly. POSIX-only:
// newlib's stdio already goes through the wrappable syscall stubs.
//
// Only the process' original stdin/stdout/stderr are routed to the app's fds; every other stream
// (real file I/O, including fwrite() to a file) goes straight to libc. fread() is not wrapped.
// putc/getc are macros, not real calls, so wrapping those symbols wouldn't reliably intercept them.

extern "C" {

int __real_vfprintf(FILE* stream, const char* format, va_list args) {
    static auto real = reinterpret_cast<int (*)(FILE*, const char*, va_list)>(dlsym(RTLD_NEXT, "vfprintf"));
    return real(stream, format, args);
}

int __real_fputs(const char* s, FILE* stream) {
    static auto real = reinterpret_cast<int (*)(const char*, FILE*)>(dlsym(RTLD_NEXT, "fputs"));
    return real(s, stream);
}

int __real_fputc(int c, FILE* stream) {
    static auto real = reinterpret_cast<int (*)(int, FILE*)>(dlsym(RTLD_NEXT, "fputc"));
    return real(c, stream);
}

size_t __real_fwrite(const void* data, size_t size, size_t count, FILE* stream) {
    static auto real = reinterpret_cast<size_t (*)(const void*, size_t, size_t, FILE*)>(dlsym(RTLD_NEXT, "fwrite"));
    return real(data, size, count, stream);
}

int __real_fgetc(FILE* stream) {
    static auto real = reinterpret_cast<int (*)(FILE*)>(dlsym(RTLD_NEXT, "fgetc"));
    return real(stream);
}

char* __real_fgets(char* buffer, int size, FILE* stream) {
    static auto real = reinterpret_cast<char* (*)(char*, int, FILE*)>(dlsym(RTLD_NEXT, "fgets"));
    return real(buffer, size, stream);
}

}

namespace {

/** @return the number of bytes written, less than `size` if the fd stopped accepting data */
size_t writeAllTo(int fd, const void* data, size_t size) {
    app_signal_deliver_pending();
    const auto* bytes = static_cast<const char*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t written = app_io_write(fd, bytes, remaining);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                app_signal_deliver_pending();
            }
            break;
        }
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }
    return size - remaining;
}

int formatTo(int fd, const char* format, va_list args) {
    char stackBuffer[256];
    va_list argsForStack;
    va_copy(argsForStack, args);
    int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), format, argsForStack);
    va_end(argsForStack);
    if (needed < 0) {
        return needed;
    }
    if (static_cast<size_t>(needed) < sizeof(stackBuffer)) {
        writeAllTo(fd, stackBuffer, static_cast<size_t>(needed));
        return needed;
    }
    auto heapBuffer = std::make_unique<char[]>(static_cast<size_t>(needed) + 1);
    va_list argsForHeap;
    va_copy(argsForHeap, args);
    vsnprintf(heapBuffer.get(), static_cast<size_t>(needed) + 1, format, argsForHeap);
    va_end(argsForHeap);
    writeAllTo(fd, heapBuffer.get(), static_cast<size_t>(needed));
    return needed;
}

int readOneFromStdin(char& out) {
    return static_cast<int>(__wrap_read(STDIN_FILENO, &out, 1));
}

// The process' own streams, captured before anything can reassign stdin/stdout/stderr. A caller
// that points stdout at a file (e.g. the shell's redirection) must get real file I/O, so only these
// original streams are routed to the app's fds.
FILE* const originalStdin = stdin;
FILE* const originalStdout = stdout;
FILE* const originalStderr = stderr;

int targetFdOf(FILE* stream) {
    if (stream == originalStdout) return STDOUT_FILENO;
    if (stream == originalStderr) return STDERR_FILENO;
    return -1;
}

} // namespace

extern "C" {

int __wrap_vprintf(const char* format, va_list args) {
    return __wrap_vfprintf(stdout, format, args);
}

int __wrap_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = __wrap_vfprintf(stdout, format, args);
    va_end(args);
    return result;
}

int __wrap_vfprintf(FILE* stream, const char* format, va_list args) {
    int fd = targetFdOf(stream);
    if (fd >= 0) {
        return formatTo(fd, format, args);
    }
    return __real_vfprintf(stream, format, args);
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int fd = targetFdOf(stream);
    int result = (fd >= 0) ? formatTo(fd, format, args) : __real_vfprintf(stream, format, args);
    va_end(args);
    return result;
}

int __wrap_puts(const char* s) {
    if (targetFdOf(stdout) < 0) {
        return __real_fputs(s, stdout) < 0 ? EOF : __real_fputc('\n', stdout);
    }
    writeAllTo(STDOUT_FILENO, s, strlen(s));
    writeAllTo(STDOUT_FILENO, "\n", 1);
    return 0;
}

int __wrap_fputs(const char* s, FILE* stream) {
    int fd = targetFdOf(stream);
    if (fd >= 0) {
        writeAllTo(fd, s, strlen(s));
        return 0;
    }
    return __real_fputs(s, stream);
}

int __wrap_putchar(int c) {
    return __wrap_fputc(c, stdout);
}

int __wrap_fputc(int c, FILE* stream) {
    int fd = targetFdOf(stream);
    if (fd >= 0) {
        auto ch = static_cast<char>(c);
        writeAllTo(fd, &ch, 1);
        return c;
    }
    return __real_fputc(c, stream);
}

size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* stream) {
    int fd = targetFdOf(stream);
    if (fd < 0) {
        return __real_fwrite(data, size, count, stream);
    }
    if (size == 0 || count == 0) {
        return 0;
    }
    return writeAllTo(fd, data, size * count) / size;
}

int __wrap_fgetc(FILE* stream) {
    if (stream != originalStdin) {
        return __real_fgetc(stream);
    }
    char c;
    return readOneFromStdin(c) == 1 ? static_cast<unsigned char>(c) : EOF;
}

int __wrap_getchar() {
    return __wrap_fgetc(stdin);
}

char* __wrap_fgets(char* buffer, int size, FILE* stream) {
    if (stream != originalStdin) {
        return __real_fgets(buffer, size, stream);
    }
    if (size <= 0) {
        return nullptr;
    }
    int i = 0;
    for (; i < size - 1; ++i) {
        char c;
        if (readOneFromStdin(c) != 1) {
            break;
        }
        buffer[i] = c;
        if (c == '\n') {
            ++i;
            break;
        }
    }
    if (i == 0) {
        return nullptr;
    }
    buffer[i] = '\0';
    return buffer;
}

}

// endregion
