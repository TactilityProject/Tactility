// SPDX-License-Identifier: Apache-2.0
// ESP-IDF's <sys/ioctl.h> has no terminal ioctls. Apps include this through app-module's include path.
// For an app fd, ioctl(fd, TIOCGWINSZ, &winsize) reports the terminal size (see app/libc.h).
#pragma once

#include_next <sys/ioctl.h>

#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x5413
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#endif
