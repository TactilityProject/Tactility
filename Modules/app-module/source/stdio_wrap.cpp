// SPDX-License-Identifier: Apache-2.0

// Platform-independent helpers for the syscall wraps in stdio_wrap_esp32.cpp and stdio_wrap_posix.cpp.
#include <app/private/stdio_wrap.h>

#include <app/dir.h>
#include <app/io.h>

#include <tactility/delay.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <cerrno>
#include <cstring>

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

AppFdState getAppFdState(int fd) {
    uint32_t bits;
    switch (app_io_poll(fd, &bits)) {
        case ERROR_NONE:
            return AppFdState::Bound;
        case ERROR_INVALID_STATE:
            return AppFdState::Closed;
        default:
            return AppFdState::NotAppFd;
    }
}

// App fds report as character devices, which also makes isatty() true for them.
bool tryAppFstat(int fd, struct stat* st, int* outResult) {
    const AppFdState state = getAppFdState(fd);
    if (state == AppFdState::NotAppFd) {
        return false;
    }
    if (state == AppFdState::Closed) {
        errno = EBADF;
        *outResult = -1;
        return true;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0666;
    *outResult = 0;
    return true;
}

// App fd input is always raw and unechoed, and a written '\n' also returns the cursor.
bool tryAppTcgetattr(int fd, struct termios* t, int* outResult) {
    const AppFdState state = getAppFdState(fd);
    if (state == AppFdState::NotAppFd) {
        return false;
    }
    if (state == AppFdState::Closed) {
        errno = EBADF;
        *outResult = -1;
        return true;
    }
    memset(t, 0, sizeof(*t));
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
    *outResult = 0;
    return true;
}

// Accepts any settings without applying them.
bool tryAppTcsetattr(int fd, int* outResult) {
    const AppFdState state = getAppFdState(fd);
    if (state == AppFdState::NotAppFd) {
        return false;
    }
    if (state == AppFdState::Closed) {
        errno = EBADF;
        *outResult = -1;
        return true;
    }
    *outResult = 0;
    return true;
}

namespace {

// Upper bound on wake latency while waiting on more than one fd: app streams can only be awaited one at a time.
constexpr TickType_t POLL_INTERVAL_TICKS = pdMS_TO_TICKS(10);

} // namespace

// Non-app fds in @a fds are checked with a zero timeout real poll() on every iteration.
bool tryAppPoll(struct pollfd* fds, nfds_t nfds, int timeout, PollFunction realPoll, int* outResult) {
    if (fds == nullptr) {
        return false;
    }
    // A closed app fd is always ready (POLLNVAL), so the wait below always has a bound fd to await
    int firstAppIndex = -1;
    bool hasAppFd = false;
    nfds_t activeCount = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) {
            continue;
        }
        activeCount++;
        const AppFdState state = getAppFdState(fds[i].fd);
        hasAppFd = hasAppFd || state != AppFdState::NotAppFd;
        if (firstAppIndex < 0 && state == AppFdState::Bound) {
            firstAppIndex = static_cast<int>(i);
        }
    }
    if (!hasAppFd) {
        return false;
    }

    const TickType_t start = get_ticks();
    const TickType_t timeoutTicks = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    while (true) {
        int ready = 0;
        for (nfds_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0) {
                continue;
            }
            uint32_t bits;
            const error_t pollResult = app_io_poll(fds[i].fd, &bits);
            if (pollResult == ERROR_NONE) {
                if ((fds[i].events & POLLIN) && (bits & APP_FILE_READABLE)) {
                    fds[i].revents |= POLLIN;
                }
                if ((fds[i].events & POLLOUT) && (bits & APP_FILE_WRITABLE)) {
                    fds[i].revents |= POLLOUT;
                }
            } else if (pollResult == ERROR_INVALID_STATE) {
                fds[i].revents = POLLNVAL;
            } else {
                realPoll(&fds[i], 1, 0);
            }
            if (fds[i].revents != 0) {
                ready++;
            }
        }
        if (ready > 0) {
            *outResult = ready;
            return true;
        }

        TickType_t remaining = (timeout < 0) ? portMAX_DELAY : get_timeout_remaining_ticks(timeoutTicks, start);
        if (remaining == 0) {
            *outResult = 0;
            return true;
        }
        if (activeCount > 1 && remaining > POLL_INTERVAL_TICKS) {
            remaining = POLL_INTERVAL_TICKS;
        }
        const struct pollfd& first = fds[firstAppIndex];
        if (first.events & POLLIN) {
            app_io_await(first.fd, APP_FILE_WAIT_READABLE, remaining);
        } else if (first.events & POLLOUT) {
            app_io_await(first.fd, APP_FILE_WAIT_WRITABLE, remaining);
        } else {
            delay_ticks(remaining < POLL_INTERVAL_TICKS ? remaining : POLL_INTERVAL_TICKS);
        }
    }
}

