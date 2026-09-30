// SPDX-License-Identifier: Apache-2.0
#include <app/libc.h>

#include <app/dir.h>
#include <app/io.h>
#include <app/private/ledger.h>
#include <app/scheduler.h>

#include <tactility/delay.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

enum class AppFdState {
    Bound,
    /** An app fd that was already closed. Fails with EBADF rather than reaching a real fd of the same number. */
    Closed,
    /** Not an app fd. Falls through to the real syscall. */
    NotAppFd,
};

AppFdState get_app_fd_state(int fd) {
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

// Upper bound on wake latency while waiting on more than one fd: app streams can only be awaited one at a time.
constexpr TickType_t POLL_INTERVAL_TICKS = pdMS_TO_TICKS(10);

} // namespace

extern "C" {

bool app_libc_try_window_size(int fd, unsigned long request, void* arg, int* out_result) {
    if (request != TIOCGWINSZ) {
        return false;
    }
    const AppFdState state = get_app_fd_state(fd);
    if (state == AppFdState::Closed) {
        errno = EBADF;
        *out_result = -1;
        return true;
    }
    if (arg == nullptr || state == AppFdState::NotAppFd) {
        return false;
    }
    AppWindowSize size {};
    if (app_io_ioctl(fd, APP_IOCTL_GET_WINDOW_SIZE, &size) != ERROR_NONE) {
        return false;
    }
    auto* out = static_cast<struct winsize*>(arg);
    out->ws_row = size.rows;
    out->ws_col = size.columns;
    out->ws_xpixel = 0;
    out->ws_ypixel = 0;
    *out_result = 0;
    return true;
}

// app_dir_get_cwd() distinguishes "not an app instance" (ERROR_NOT_FOUND, caller falls through to the
// real syscall) from "buffer too small" (ERROR_BUFFER_OVERFLOW, a real failure to report).
bool app_libc_try_getcwd(char* buf, size_t size, char** out_result) {
    if (buf == nullptr) {
        return false;
    }
    const error_t result = app_dir_get_cwd(buf, size);
    if (result == ERROR_NONE) {
        *out_result = buf;
        return true;
    }
    if (result == ERROR_BUFFER_OVERFLOW) {
        errno = ERANGE;
        *out_result = nullptr;
        return true;
    }
    return false; // ERROR_NOT_FOUND: not an app instance.
}

// app_dir_set_cwd() requires an already-absolute path and reports both "not an app instance" and
// "no such directory" as ERROR_NOT_FOUND, so app_dir_get_cwd() is used first as an unambiguous
// "is this an app instance" probe (it's needed anyway, to resolve a relative path).
bool app_libc_try_chdir(const char* path, int* out_result) {
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
        errno = ENAMETOOLONG;
        *out_result = -1;
        return true;
    }

    if (app_dir_set_cwd(resolved) == ERROR_NONE) {
        *out_result = 0;
    } else {
        errno = ENOENT;
        *out_result = -1;
    }
    return true;
}

bool app_libc_try_fstat(int fd, struct stat* st, int* out_result) {
    const AppFdState state = get_app_fd_state(fd);
    if (state == AppFdState::NotAppFd) {
        return false;
    }
    if (state == AppFdState::Closed) {
        errno = EBADF;
        *out_result = -1;
        return true;
    }
    if (st == nullptr) {
        errno = EFAULT;
        *out_result = -1;
        return true;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0666;
    *out_result = 0;
    return true;
}

bool app_libc_try_tcgetattr(int fd, struct termios* t, int* out_result) {
    const AppFdState state = get_app_fd_state(fd);
    if (state == AppFdState::NotAppFd) {
        return false;
    }
    if (state == AppFdState::Closed) {
        errno = EBADF;
        *out_result = -1;
        return true;
    }
    if (t == nullptr) {
        errno = EFAULT;
        *out_result = -1;
        return true;
    }
    tcflag_t iflag = ICRNL;
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_scheduler_current_app_id());
    if (iterator != ledger.instances.end()) {
        iflag = iterator->second.termios_iflag;
    }
    mutex_unlock(&ledger.mutex);
    memset(t, 0, sizeof(*t));
    t->c_iflag = iflag;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
    *out_result = 0;
    return true;
}

bool app_libc_try_tcsetattr(int fd, const struct termios* t, int* out_result) {
    const AppFdState state = get_app_fd_state(fd);
    if (state == AppFdState::NotAppFd) {
        return false;
    }
    if (state == AppFdState::Closed) {
        errno = EBADF;
        *out_result = -1;
        return true;
    }
    if (t == nullptr) {
        errno = EFAULT;
        *out_result = -1;
        return true;
    }
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_scheduler_current_app_id());
    if (iterator != ledger.instances.end()) {
        iterator->second.termios_iflag = t->c_iflag;
    }
    mutex_unlock(&ledger.mutex);
    *out_result = 0;
    return true;
}

bool app_libc_try_signal(int sig, AppLibcSignalHandler handler, AppLibcSignalHandler* out_previous) {
    const AppInstanceId app_instance_id = app_scheduler_current_app_id();
    if (app_instance_id == 0) {
        return false;
    }
    if (sig <= 0 || sig >= APP_LIBC_SIGNAL_COUNT || sig == SIGKILL || sig == SIGSTOP) {
        errno = EINVAL;
        *out_previous = SIG_ERR;
        return true;
    }
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        return false;
    }
    AppLibcSignalHandler previous = iterator->second.signal_handlers[sig];
    iterator->second.signal_handlers[sig] = handler;
    mutex_unlock(&ledger.mutex);
    *out_previous = previous;
    return true;
}

bool app_libc_try_kill(int pid, int sig, int* out_result) {
    if (app_scheduler_current_app_id() == 0) {
        return false;
    }
    errno = ENOSYS;
    *out_result = -1;
    return true;
}

// Non-app fds in @a fds are checked with a zero timeout real poll() on every iteration.
bool app_libc_try_poll(struct pollfd* fds, nfds_t nfds, int timeout, AppLibcPollFunction real_poll, int* out_result) {
    if (fds == nullptr) {
        return false;
    }
    // A closed app fd is always ready (POLLNVAL), so the wait below always has a bound fd to await
    int first_app_index = -1;
    bool has_app_fd = false;
    nfds_t active_count = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) {
            continue;
        }
        active_count++;
        const AppFdState state = get_app_fd_state(fds[i].fd);
        has_app_fd = has_app_fd || state != AppFdState::NotAppFd;
        if (first_app_index < 0 && state == AppFdState::Bound) {
            first_app_index = static_cast<int>(i);
        }
    }
    if (!has_app_fd) {
        return false;
    }

    const TickType_t start = get_ticks();
    const TickType_t timeout_ticks = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    while (true) {
        int ready = 0;
        for (nfds_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0) {
                continue;
            }
            uint32_t bits;
            const error_t poll_result = app_io_poll(fds[i].fd, &bits);
            if (poll_result == ERROR_NONE) {
                if ((fds[i].events & POLLIN) && (bits & APP_FILE_READABLE)) {
                    fds[i].revents |= POLLIN;
                }
                if ((fds[i].events & POLLOUT) && (bits & APP_FILE_WRITABLE)) {
                    fds[i].revents |= POLLOUT;
                }
            } else if (poll_result == ERROR_INVALID_STATE) {
                fds[i].revents = POLLNVAL;
            } else {
                real_poll(&fds[i], 1, 0);
            }
            if (fds[i].revents != 0) {
                ready++;
            }
        }
        if (ready > 0) {
            *out_result = ready;
            return true;
        }

        TickType_t remaining = (timeout < 0) ? portMAX_DELAY : get_timeout_remaining_ticks(timeout_ticks, start);
        if (remaining == 0) {
            *out_result = 0;
            return true;
        }
        if (active_count > 1 && remaining > POLL_INTERVAL_TICKS) {
            remaining = POLL_INTERVAL_TICKS;
        }
        const struct pollfd& first = fds[first_app_index];
        if (first.events & POLLIN) {
            app_io_await(first.fd, APP_FILE_WAIT_READABLE, remaining);
        } else if (first.events & POLLOUT) {
            app_io_await(first.fd, APP_FILE_WAIT_WRITABLE, remaining);
        } else {
            delay_ticks(remaining < POLL_INTERVAL_TICKS ? remaining : POLL_INTERVAL_TICKS);
        }
    }
}

} // extern "C"
