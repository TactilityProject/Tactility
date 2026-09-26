// SPDX-License-Identifier: Apache-2.0

// ESP32 uses -Wl,--wrap=. POSIX can't: --wrap doesn't reach a dlopen()ed app's own printf/write
// calls, so these are defined under their real names instead - dyld interpose on Apple, plain
// strong definitions elsewhere (ELF gives the main executable's symbols priority process-wide).
#include <app/dir.h>
#include <app/io.h>

#include <tactility/paths.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/types.h>

// Neither platform's headers declare TIOCGWINSZ/struct winsize (ESP-IDF's sys/ioctl.h has no
// terminal ioctls at all; the guard is only for POSIX, where <sys/ioctl.h> already provides them).
#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x5413
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#endif

namespace {

// Shared by both platforms' ioctl wraps: true (with *out filled) if this was a window-size query
// on an app-owned fd, so the caller can skip falling through to the real syscall.
bool tryAppWindowSize(int fd, unsigned long request, void* arg, struct winsize* out) {
    if (request != TIOCGWINSZ || arg == nullptr) {
        return false;
    }
    AppWindowSize size {};
    if (app_io_ioctl(fd, APP_IOCTL_GET_WINDOW_SIZE, &size) != ERROR_NONE) {
        return false;
    }
    out->ws_row = size.rows;
    out->ws_col = size.columns;
    out->ws_xpixel = 0;
    out->ws_ypixel = 0;
    return true;
}

// getcwd(): app_dir_get_cwd() distinguishes "not an app instance" (ERROR_NOT_FOUND, caller falls
// through to the real syscall) from "buffer too small" (ERROR_BUFFER_OVERFLOW, a real failure to
// report), so no separate probe is needed here.
bool tryAppGetCwd(char* buf, size_t size, char** out, int* outErrno) {
    if (buf == nullptr) {
        return false;
    }
    const error_t result = app_dir_get_cwd(buf, size);
    if (result == ERROR_NONE) {
        *out = buf;
        return true;
    }
    if (result == ERROR_BUFFER_OVERFLOW) {
        *out = nullptr;
        *outErrno = ERANGE;
        return true;
    }
    return false; // ERROR_NOT_FOUND: not an app instance.
}

// chdir(): app_dir_set_cwd() requires an already-absolute path and reports both "not an app
// instance" and "no such directory" as ERROR_NOT_FOUND, so app_dir_get_cwd() is used first as a
// cheap, unambiguous "is this an app instance" probe (it's needed anyway, to resolve a relative
// path) - only once that confirms an app instance is calling is app_dir_set_cwd()'s own result
// treated as a real success/failure to report, rather than a reason to fall through.
bool tryAppChdir(const char* path, int* outResult, int* outErrno) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    char cwd[FILE_MAX_PATH_STRING_LENGTH];
    if (app_dir_get_cwd(cwd, sizeof(cwd)) != ERROR_NONE) {
        return false; // not an app instance
    }

    char resolved[FILE_MAX_PATH_STRING_LENGTH];
    const int written = (path[0] == '/') ? snprintf(resolved, sizeof(resolved), "%s", path)
        : (strcmp(cwd, "/") == 0) ? snprintf(resolved, sizeof(resolved), "/%s", path)
        : snprintf(resolved, sizeof(resolved), "%s/%s", cwd, path);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(resolved)) {
        *outResult = -1;
        *outErrno = ENAMETOOLONG;
        return true;
    }

    if (app_dir_set_cwd(resolved) == ERROR_NONE) {
        *outResult = 0;
    } else {
        *outResult = -1;
        *outErrno = ENOENT;
    }
    return true;
}

} // namespace

#ifdef ESP_PLATFORM

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

}

#else

extern "C" int __real_ioctl(int fd, unsigned long request, void* arg);

extern "C" {

ssize_t __wrap_read(int fd, void* buffer, size_t size) {
    return app_io_read(fd, buffer, size);
}

ssize_t __wrap_write(int fd, const void* buffer, size_t size) {
    return app_io_write(fd, buffer, size);
}

int __wrap_close(int fd) {
    return app_io_close(fd);
}

int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void* arg = va_arg(args, void*);
    va_end(args);

    struct winsize windowSize {};
    if (tryAppWindowSize(fd, request, arg, &windowSize)) {
        *static_cast<struct winsize*>(arg) = windowSize;
        return 0;
    }
    return __real_ioctl(fd, request, arg);
}

}

// dlsym(RTLD_NEXT, ...) avoids recursing into our own override below.
#include <dlfcn.h>
#include <unistd.h>

extern "C" char* __real_getcwd(char* buf, size_t size);
extern "C" int __real_chdir(const char* path);

extern "C" {

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

}

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

}

#ifdef __APPLE__

// <mach-o/dyld-interposing.h> isn't a public SDK header, so reimplemented locally.
#define TT_DYLD_INTERPOSE(replacement, replacee) \
    __attribute__((used)) static struct { const void* replacement; const void* replacee; } \
        tt_interpose_##replacee __attribute__((section("__DATA,__interpose"))) = { \
            (const void*)(unsigned long)&(replacement), (const void*)(unsigned long)&(replacee) \
        };

TT_DYLD_INTERPOSE(__wrap_read, read)
TT_DYLD_INTERPOSE(__wrap_write, write)
TT_DYLD_INTERPOSE(__wrap_ioctl, ioctl)
TT_DYLD_INTERPOSE(__wrap_close, close)
TT_DYLD_INTERPOSE(__wrap_getcwd, getcwd)
TT_DYLD_INTERPOSE(__wrap_chdir, chdir)

#else

extern "C" {

ssize_t read(int fd, void* buffer, size_t size) {
    return __wrap_read(fd, buffer, size);
}

ssize_t write(int fd, const void* buffer, size_t size) {
    return __wrap_write(fd, buffer, size);
}

int close(int fd) {
    return __wrap_close(fd);
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void* arg = va_arg(args, void*);
    va_end(args);
    return __wrap_ioctl(fd, request, arg);
}

char* getcwd(char* buf, size_t size) {
    return __wrap_getcwd(buf, size);
}

int chdir(const char* path) {
    return __wrap_chdir(path);
}

}

#endif // __APPLE__

#endif // ESP_PLATFORM

// region glibc stdio wraps
//
// libc's printf/fprintf/etc call an internal, non-exported write() alias that the read/write/close
// wraps above can't reach, so these redirect calls to printf/fprintf/etc directly. POSIX-only:
// newlib's stdio already goes through the wrappable syscall stubs.
//
// Only the process' original stdin/stdout/stderr are routed to the app's fds; every other stream
// (real file I/O, including fwrite() to a file) goes straight to libc. fread() is not wrapped.
// putc/getc are macros, not real calls, so wrapping those symbols wouldn't reliably intercept them.

#if !defined(ESP_PLATFORM)

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>

extern "C" {
int __real_vfprintf(FILE* stream, const char* format, va_list args);
int __real_fputs(const char* s, FILE* stream);
int __real_fputc(int c, FILE* stream);
size_t __real_fwrite(const void* data, size_t size, size_t count, FILE* stream);
int __real_fgetc(FILE* stream);
char* __real_fgets(char* buffer, int size, FILE* stream);
}

#include <dlfcn.h>

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

void writeAllTo(int fd, const void* data, size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t written = app_io_write(fd, bytes, remaining);
        if (written <= 0) {
            break;
        }
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }
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
    return static_cast<int>(app_io_read(STDIN_FILENO, &out, 1));
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

int __wrap_vfprintf(FILE* stream, const char* format, va_list args);
int __wrap_fputc(int c, FILE* stream);

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
    writeAllTo(fd, data, size * count);
    return count;
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

#ifdef __APPLE__
TT_DYLD_INTERPOSE(__wrap_vprintf, vprintf)
TT_DYLD_INTERPOSE(__wrap_printf, printf)
TT_DYLD_INTERPOSE(__wrap_vfprintf, vfprintf)
TT_DYLD_INTERPOSE(__wrap_fprintf, fprintf)
TT_DYLD_INTERPOSE(__wrap_puts, puts)
TT_DYLD_INTERPOSE(__wrap_fputs, fputs)
TT_DYLD_INTERPOSE(__wrap_putchar, putchar)
TT_DYLD_INTERPOSE(__wrap_fputc, fputc)
TT_DYLD_INTERPOSE(__wrap_fwrite, fwrite)
TT_DYLD_INTERPOSE(__wrap_getchar, getchar)
TT_DYLD_INTERPOSE(__wrap_fgetc, fgetc)
TT_DYLD_INTERPOSE(__wrap_fgets, fgets)
#else

extern "C" {

int vprintf(const char* format, va_list args) {
    return __wrap_vprintf(format, args);
}

int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = __wrap_vprintf(format, args);
    va_end(args);
    return result;
}

int vfprintf(FILE* stream, const char* format, va_list args) {
    return __wrap_vfprintf(stream, format, args);
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = __wrap_vfprintf(stream, format, args);
    va_end(args);
    return result;
}

int puts(const char* s) {
    return __wrap_puts(s);
}

int fputs(const char* s, FILE* stream) {
    return __wrap_fputs(s, stream);
}

int putchar(int c) {
    return __wrap_putchar(c);
}

int fputc(int c, FILE* stream) {
    return __wrap_fputc(c, stream);
}

size_t fwrite(const void* data, size_t size, size_t count, FILE* stream) {
    return __wrap_fwrite(data, size, count, stream);
}

int getchar() {
    return __wrap_getchar();
}

int fgetc(FILE* stream) {
    return __wrap_fgetc(stream);
}

char* fgets(char* buffer, int size, FILE* stream) {
    return __wrap_fgets(buffer, size, stream);
}

}

#endif // __APPLE__

#endif // !ESP_PLATFORM

// endregion
