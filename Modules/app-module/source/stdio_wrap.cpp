// SPDX-License-Identifier: Apache-2.0

// ESP32 uses -Wl,--wrap=. POSIX can't: --wrap doesn't reach a dlopen()ed app's own printf/write
// calls, so these are defined under their real names instead - dyld interpose on Apple, plain
// strong definitions elsewhere (ELF gives the main executable's symbols priority process-wide).
#include <app/io.h>

#include <sys/types.h>

#ifdef ESP_PLATFORM

// Newlib's own stdio calls the reentrant _read_r/_write_r/_close_r stubs directly, not the plain
// read/write/close wrappers, so those stubs are wrapped instead of the plain names.
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

}

#else

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

}

// dlsym(RTLD_NEXT, ...) avoids recursing into our own override below.
#include <dlfcn.h>
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
TT_DYLD_INTERPOSE(__wrap_close, close)

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

}

#endif // __APPLE__

#endif // ESP_PLATFORM

// region glibc stdio wraps
//
// libc's printf/fprintf/etc call an internal, non-exported write() alias that the read/write/close
// wraps above can't reach, so these redirect calls to printf/fprintf/etc directly. POSIX-only:
// newlib's stdio already goes through the wrappable syscall stubs.
//
// Scoped to printf/getc: fread/fwrite are used sitewide for real file I/O, so wrapping them would
// be a correctness risk for unrelated code. putc/getc are macros, not real calls, so wrapping
// those symbols wouldn't reliably intercept them.

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

int targetFdOf(FILE* stream) {
    if (stream == stdout) return STDOUT_FILENO;
    if (stream == stderr) return STDERR_FILENO;
    return -1;
}

} // namespace

extern "C" {

int __wrap_vprintf(const char* format, va_list args) {
    return formatTo(STDOUT_FILENO, format, args);
}

int __wrap_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = formatTo(STDOUT_FILENO, format, args);
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
    auto ch = static_cast<char>(c);
    writeAllTo(STDOUT_FILENO, &ch, 1);
    return c;
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

int __wrap_getchar() {
    char c;
    return readOneFromStdin(c) == 1 ? static_cast<unsigned char>(c) : EOF;
}

int __wrap_fgetc(FILE* stream) {
    if (stream == stdin) {
        return __wrap_getchar();
    }
    return __real_fgetc(stream);
}

char* __wrap_fgets(char* buffer, int size, FILE* stream) {
    if (stream != stdin) {
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
