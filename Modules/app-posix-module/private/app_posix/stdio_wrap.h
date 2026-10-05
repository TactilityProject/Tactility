// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <app/libc.h>

#include <signal.h>
#include <termios.h>
#include <unistd.h>


// Implemented in stdio_wrap.cpp, installed under the real names by stdio_wrap_elf.cpp or stdio_wrap_apple.cpp.
extern "C" {
ssize_t __wrap_read(int fd, void* buffer, size_t size);
ssize_t __wrap_write(int fd, const void* buffer, size_t size);
int __wrap_close(int fd);
int __wrap_ioctl(int fd, unsigned long request, ...);
char* __wrap_getcwd(char* buf, size_t size);
int __wrap_chdir(const char* path);
int __wrap_fstat(int fd, struct stat* st);
int __wrap_poll(struct pollfd* fds, nfds_t nfds, int timeout);
int __wrap_tcgetattr(int fd, struct termios* p);
int __wrap_tcsetattr(int fd, int optional_actions, const struct termios* p);
AppLibcSignalHandler __wrap_signal(int sig, AppLibcSignalHandler handler);
int __wrap_kill(pid_t pid, int sig);
pid_t __wrap_getpid();
pid_t __wrap_getppid();
int __wrap_usleep(useconds_t usec);
unsigned int __wrap_sleep(unsigned int seconds);
[[noreturn]] void __wrap_exit(int status);

int __wrap_open(const char* path, int flags, ...);
FILE* __wrap_fopen(const char* path, const char* mode);
int __wrap_stat(const char* path, struct stat* st);
int __wrap_lstat(const char* path, struct stat* st);
int __wrap_access(const char* path, int mode);
int __wrap_unlink(const char* path);
int __wrap_remove(const char* path);
int __wrap_rename(const char* src, const char* dst);
int __wrap_mkdir(const char* path, mode_t mode);
int __wrap_rmdir(const char* path);
DIR* __wrap_opendir(const char* path);
int __real_fclose(FILE* file);
FILE* __real_fdopen(int fd, const char* mode);
int __real_closedir(DIR* dir);
int __wrap_truncate(const char* path, off_t length);

int __wrap_vprintf(const char* format, va_list args);
int __wrap_printf(const char* format, ...);
int __wrap_vfprintf(FILE* stream, const char* format, va_list args);
int __wrap_fprintf(FILE* stream, const char* format, ...);
int __wrap_puts(const char* s);
int __wrap_fputs(const char* s, FILE* stream);
int __wrap_putchar(int c);
int __wrap_fputc(int c, FILE* stream);
size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* stream);
int __wrap_getchar();
int __wrap_fgetc(FILE* stream);
char* __wrap_fgets(char* buffer, int size, FILE* stream);
}

