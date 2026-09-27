// SPDX-License-Identifier: Apache-2.0
#if !defined(ESP_PLATFORM) && !defined(__APPLE__)

// Plain strong definitions: ELF gives the main executable's symbols priority process-wide,
// including for a dlopen()ed app's own calls.
#include <app/private/stdio_wrap_posix.h>

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

int fstat(int fd, struct stat* st) {
    return __wrap_fstat(fd, st);
}

int poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    return __wrap_poll(fds, nfds, timeout);
}

int tcgetattr(int fd, struct termios* p) {
    return __wrap_tcgetattr(fd, p);
}

int tcsetattr(int fd, int optional_actions, const struct termios* p) {
    return __wrap_tcsetattr(fd, optional_actions, p);
}

void exit(int status) {
    __wrap_exit(status);
}

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

#endif
