// SPDX-License-Identifier: Apache-2.0
#ifndef __APPLE__

// Plain strong definitions: ELF gives the main executable's symbols priority process-wide,
// including for a dlopen()ed app's own calls.
// Files opened by an app's own code are tracked here, where the caller is still known (see app/resources.h).
#include <app_posix/malloc_wrap.h>
#include <app_posix/stdio_wrap.h>

#include <app/resources.h>

extern "C" {

ssize_t read(int fd, void* buffer, size_t size) {
    return __wrap_read(fd, buffer, size);
}

ssize_t write(int fd, const void* buffer, size_t size) {
    return __wrap_write(fd, buffer, size);
}

int close(int fd) {
    if (app_posix_is_app_caller(__builtin_return_address(0))) {
        app_resources_untrack_fd(fd);
    }
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

AppLibcSignalHandler signal(int sig, AppLibcSignalHandler handler) {
    return __wrap_signal(sig, handler);
}

int kill(pid_t pid, int sig) {
    return __wrap_kill(pid, sig);
}

pid_t getpid() {
    return __wrap_getpid();
}

pid_t getppid() {
    return __wrap_getppid();
}

int usleep(useconds_t usec) {
    return __wrap_usleep(usec);
}

unsigned int sleep(unsigned int seconds) {
    return __wrap_sleep(seconds);
}

void exit(int status) {
    __wrap_exit(status);
}

int open(const char* path, int flags, ...) {
    // The mode is only passed (and read) when the file can be created
    bool has_mode = (flags & O_CREAT) != 0;
#ifdef O_TMPFILE
    has_mode = has_mode || (flags & O_TMPFILE) == O_TMPFILE;
#endif
    mode_t mode = 0;
    if (has_mode) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    const int fd = __wrap_open(path, flags, mode);
    if (fd >= 0 && app_posix_is_app_caller(__builtin_return_address(0))) {
        app_resources_track_fd(fd);
    }
    return fd;
}

FILE* fopen(const char* path, const char* mode) {
    FILE* file = __wrap_fopen(path, mode);
    if (file != nullptr && app_posix_is_app_caller(__builtin_return_address(0))) {
        app_resources_track_file(file);
    }
    return file;
}

FILE* fdopen(int fd, const char* mode) {
    FILE* file = __real_fdopen(fd, mode);
    if (file != nullptr && app_posix_is_app_caller(__builtin_return_address(0))) {
        // Closed through the FILE from now on
        app_resources_untrack_fd(fd);
        app_resources_track_file(file);
    }
    return file;
}

int fclose(FILE* file) {
    if (app_posix_is_app_caller(__builtin_return_address(0))) {
        app_resources_untrack_file(file);
    }
    return __real_fclose(file);
}

int stat(const char* path, struct stat* st) {
    return __wrap_stat(path, st);
}

int lstat(const char* path, struct stat* st) {
    return __wrap_lstat(path, st);
}

int access(const char* path, int mode) {
    return __wrap_access(path, mode);
}

int unlink(const char* path) {
    return __wrap_unlink(path);
}

int remove(const char* path) {
    return __wrap_remove(path);
}

int rename(const char* src, const char* dst) {
    return __wrap_rename(src, dst);
}

int mkdir(const char* path, mode_t mode) {
    return __wrap_mkdir(path, mode);
}

int rmdir(const char* path) {
    return __wrap_rmdir(path);
}

DIR* opendir(const char* path) {
    DIR* dir = __wrap_opendir(path);
    if (dir != nullptr && app_posix_is_app_caller(__builtin_return_address(0))) {
        app_resources_track_dir(dir);
    }
    return dir;
}

int closedir(DIR* dir) {
    if (app_posix_is_app_caller(__builtin_return_address(0))) {
        app_resources_untrack_dir(dir);
    }
    return __real_closedir(dir);
}

int truncate(const char* path, off_t length) {
    return __wrap_truncate(path, length);
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
