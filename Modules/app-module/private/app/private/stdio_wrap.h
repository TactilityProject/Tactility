// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdio>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>

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

using PollFunction = int (*)(struct pollfd*, nfds_t, int);

/** @return true (with *out filled) if this was a window-size query on an app-owned fd */
bool tryAppWindowSize(int fd, unsigned long request, void* arg, struct winsize* out);

/** @return false if the caller isn't an app instance and should fall through to the real getcwd() */
bool tryAppGetCwd(char* buf, size_t size, char** out, int* outErrno);

/** @return false if the caller isn't an app instance and should fall through to the real chdir() */
bool tryAppChdir(const char* path, int* outResult, int* outErrno);

bool isAppFd(int fd);

/** @return false if @a fd isn't app-bound and the caller should fall through to the real fstat() */
bool tryAppFstat(int fd, struct stat* st);

void fillAppTermios(struct termios* t);

/** @return false if no fd in @a fds is app-bound and the caller should fall through to @a realPoll */
bool tryAppPoll(struct pollfd* fds, nfds_t nfds, int timeout, PollFunction realPoll, int* outResult);
