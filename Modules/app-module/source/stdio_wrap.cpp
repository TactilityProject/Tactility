// SPDX-License-Identifier: Apache-2.0

// Paired with -Wl,--wrap=read/write/close on POSIX (this module's own CMakeLists.txt) and ESP32
// (top-level CMakeLists.txt); self-registered via dyld interpose below on Apple, whose linker
// doesn't support --wrap.
#include <app/io.h>

#include <sys/types.h>

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

#ifdef __APPLE__

// --wrap also synthesizes __real_read/write/close automatically; dyld interpose doesn't, so
// io.cpp's TT_APP_IO_WRAPS_STDIO fallback needs them defined here. dlsym(RTLD_NEXT, ...) is the
// standard way to reach the true libSystem implementation despite the interpose below: a direct
// call to read/write/close from this file would just recurse into __wrap_read/write/close, since
// interpose rewrites every reference to those symbols in the process, this file included.
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

// <mach-o/dyld-interposing.h> isn't a public SDK header (it ships with dyld's own source, not
// Xcode/Command Line Tools), so this reimplements its DYLD_INTERPOSE macro locally; reused below
// for the printf-family interposes too.
#define TT_DYLD_INTERPOSE(replacement, replacee) \
    __attribute__((used)) static struct { const void* replacement; const void* replacee; } \
        tt_interpose_##replacee __attribute__((section("__DATA,__interpose"))) = { \
            (const void*)(unsigned long)&(replacement), (const void*)(unsigned long)&(replacee) \
        };

TT_DYLD_INTERPOSE(__wrap_read, read)
TT_DYLD_INTERPOSE(__wrap_write, write)
TT_DYLD_INTERPOSE(__wrap_close, close)

#endif // __APPLE__

// region glibc stdio wraps
//
// libc's printf/fprintf/etc are compiled into the C library and call an internal, non-exported
// write() alias, which --wrap=write/the read/write/close interpose above can't reach: only calls
// WE make to the public symbol. These wraps instead redirect calls WE make to printf/fprintf/etc,
// the same trick as read/write/close above. Newlib (ESP-IDF) doesn't have this gap: its stdio does
// call the wrappable syscall stubs, so this block is POSIX-only.
//
// Scoped to the printf/getc families only: fread/fwrite take an arbitrary FILE* and are already
// used sitewide for real file I/O (e.g. File.cpp's readBinaryInternal), so wrapping them would
// route every such call through this file's stdin/stdout check, a correctness risk for unrelated
// code that isn't worth taking here. putc/getc are excluded too since libc defines them as
// macros, not real calls, so wrapping those symbols wouldn't reliably intercept them.

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

#ifdef __APPLE__

// --wrap synthesizes these automatically elsewhere; on Apple they're defined here via
// dlsym(RTLD_NEXT, ...) instead. See the read/write/close __real_* block above for why.
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

#endif // __APPLE__

namespace {

void writeAllToStdout(const void* data, size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t written = app_io_write(STDOUT_FILENO, bytes, remaining);
        if (written <= 0) {
            break;
        }
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }
}

// Formats into stdout via app_io_write() rather than through a FILE*'s own buffering, since that
// buffering is exactly what glibc's internal write() call sidesteps --wrap for in the first place.
int formatToStdout(const char* format, va_list args) {
    char stackBuffer[256];
    va_list argsForStack;
    va_copy(argsForStack, args);
    int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), format, argsForStack);
    va_end(argsForStack);
    if (needed < 0) {
        return needed;
    }
    if (static_cast<size_t>(needed) < sizeof(stackBuffer)) {
        writeAllToStdout(stackBuffer, static_cast<size_t>(needed));
        return needed;
    }
    auto heapBuffer = std::make_unique<char[]>(static_cast<size_t>(needed) + 1);
    va_list argsForHeap;
    va_copy(argsForHeap, args);
    vsnprintf(heapBuffer.get(), static_cast<size_t>(needed) + 1, format, argsForHeap);
    va_end(argsForHeap);
    writeAllToStdout(heapBuffer.get(), static_cast<size_t>(needed));
    return needed;
}

int readOneFromStdin(char& out) {
    return static_cast<int>(app_io_read(STDIN_FILENO, &out, 1));
}

} // namespace

extern "C" {

int __wrap_vprintf(const char* format, va_list args) {
    return formatToStdout(format, args);
}

int __wrap_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = formatToStdout(format, args);
    va_end(args);
    return result;
}

int __wrap_vfprintf(FILE* stream, const char* format, va_list args) {
    if (stream == stdout) {
        return formatToStdout(format, args);
    }
    return __real_vfprintf(stream, format, args);
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = (stream == stdout) ? formatToStdout(format, args) : __real_vfprintf(stream, format, args);
    va_end(args);
    return result;
}

int __wrap_puts(const char* s) {
    writeAllToStdout(s, strlen(s));
    writeAllToStdout("\n", 1);
    return 0;
}

int __wrap_fputs(const char* s, FILE* stream) {
    if (stream == stdout) {
        writeAllToStdout(s, strlen(s));
        return 0;
    }
    return __real_fputs(s, stream);
}

int __wrap_putchar(int c) {
    auto ch = static_cast<char>(c);
    writeAllToStdout(&ch, 1);
    return c;
}

int __wrap_fputc(int c, FILE* stream) {
    if (stream == stdout) {
        return __wrap_putchar(c);
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
#endif // __APPLE__

#endif // !ESP_PLATFORM

// endregion
