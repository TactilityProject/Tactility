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

enum class AppFdState {
    Bound,
    /** An app fd that was already closed. Fails with EBADF rather than reaching a real fd of the same number. */
    Closed,
    /** Not an app fd. Falls through to the real syscall. */
    NotAppFd,
};

AppFdState getAppFdState(int fd);

/**
 * The tryApp*() fd helpers below set *outResult (and errno on failure) when they return true.
 * @return false if @a fd isn't an app fd and the caller should fall through to the real call
 */
bool tryAppFstat(int fd, struct stat* st, int* outResult);
bool tryAppTcgetattr(int fd, struct termios* t, int* outResult);
bool tryAppTcsetattr(int fd, int* outResult);

/**
 * Closed app fds report POLLNVAL.
 * @return false if no fd in @a fds is an app fd and the caller should fall through to @a realPoll
 */
bool tryAppPoll(struct pollfd* fds, nfds_t nfds, int timeout, PollFunction realPoll, int* outResult);
