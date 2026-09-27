// SPDX-License-Identifier: Apache-2.0
#ifndef ESP_PLATFORM

// POSIX can't use -Wl,--wrap= like ESP32: --wrap doesn't reach a dlopen()ed app's own printf/write
// calls, so these wraps are installed under their real names instead - dyld interpose on Apple
// (stdio_wrap_apple.cpp), plain strong definitions elsewhere (stdio_wrap_elf.cpp).
#include <app/private/stdio_wrap.h>
#include <app/private/stdio_wrap_posix.h>

#include <app/io.h>

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

int __wrap_fstat(int fd, struct stat* st) {
    int result;
    if (tryAppFstat(fd, st, &result)) {
        return result;
    }
    return __real_fstat(fd, st);
}

int __wrap_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    int result;
    if (tryAppPoll(fds, nfds, timeout, __real_poll, &result)) {
        return result;
    }
    return __real_poll(fds, nfds, timeout);
}

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

#endif // ESP_PLATFORM
