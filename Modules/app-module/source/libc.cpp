// SPDX-License-Identifier: Apache-2.0
#include <app/libc.h>

#include <app/dir.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/private/ledger.h>
#include <app/scheduler.h>
#include <app/signal.h>

#include <tactility/delay.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
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

// Upper bound on how long a sleep or poll takes to notice a signal (see app/signal.h)
constexpr TickType_t SIGNAL_CHECK_INTERVAL_TICKS = pdMS_TO_TICKS(100);

/**
 * @param[out] out_remaining the ticks not slept
 * @return true when interrupted by a signal
 */
bool sleep_unless_signalled(TickType_t ticks, TickType_t* out_remaining) {
    const TickType_t start = get_ticks();
    while (true) {
        const TickType_t remaining = get_timeout_remaining_ticks(ticks, start);
        *out_remaining = remaining;
        if (app_signal_is_pending()) {
            return true;
        }
        if (remaining == 0) {
            return false;
        }
        delay_ticks(remaining < SIGNAL_CHECK_INTERVAL_TICKS ? remaining : SIGNAL_CHECK_INTERVAL_TICKS);
    }
}

#ifdef ESP_PLATFORM
/**
 * Collapses the ".", ".." and empty segments of an absolute path, in place.
 * Every segment written is preceded by a '/' that was read, so writing never overtakes reading.
 */
void normalize_path(char* path) {
    size_t length = 0;
    const char* segment = path;
    while (*segment != '\0') {
        while (*segment == '/') {
            segment++;
        }
        const char* end = segment;
        while (*end != '\0' && *end != '/') {
            end++;
        }
        const size_t segment_length = end - segment;
        if (segment_length == 2 && segment[0] == '.' && segment[1] == '.') {
            while (length > 0 && path[length - 1] != '/') {
                length--;
            }
            if (length > 0) {
                length--; // the '/' before the removed segment
            }
        } else if (segment_length > 0 && !(segment_length == 1 && segment[0] == '.')) {
            path[length++] = '/';
            memmove(path + length, segment, segment_length);
            length += segment_length;
        }
        segment = end;
    }
    if (length == 0) {
        path[length++] = '/';
    }
    path[length] = '\0';
}
#endif

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

// The cwd is written to buf directly and the path appended to it, so a relative path needs no second buffer.
bool app_libc_try_resolve_path(const char* path, char* buf, size_t size, const char** out_path) {
    // Checked before app_dir_get_cwd() takes the ledger's lock: every path-based libc call in the
    // process ends up here, including the simulator's foreign threads (e.g. SDL's) and early boot.
    if (path == nullptr || path[0] == '\0' || app_scheduler_current_app_id() == 0) {
        return false;
    }
    const error_t cwd_result = app_dir_get_cwd(buf, size);
    if (cwd_result == ERROR_NOT_FOUND) {
        return false; // not an app instance
    }

    const size_t path_length = strlen(path);
    if (path[0] == '/') {
        if (path_length >= size) {
            errno = ENAMETOOLONG;
            *out_path = nullptr;
            return true;
        }
        memcpy(buf, path, path_length + 1);
    } else {
        const size_t cwd_length = (cwd_result == ERROR_NONE) ? strlen(buf) : size;
        // No separator after the root, which already ends with one
        const size_t separator_length = (cwd_length > 0 && cwd_length < size && buf[cwd_length - 1] == '/') ? 0 : 1;
        if (cwd_length + separator_length + path_length >= size) {
            errno = ENAMETOOLONG;
            *out_path = nullptr;
            return true;
        }
        if (separator_length != 0) {
            buf[cwd_length] = '/';
        }
        memcpy(buf + cwd_length + separator_length, path, path_length + 1);
    }

#ifdef ESP_PLATFORM
    normalize_path(buf);
#endif
    *out_path = buf;
    return true;
}

// app_dir_set_cwd() requires an already-absolute path and reports both "not an app instance" and
// "no such directory" as ERROR_NOT_FOUND, so the path is resolved first, which also tells the two apart.
bool app_libc_try_chdir(const char* path, int* out_result) {
    char resolved[FILE_MAX_PATH_STRING_LENGTH];
    const char* resolved_path;
    if (!app_libc_try_resolve_path(path, resolved, sizeof(resolved), &resolved_path)) {
        return false;
    }
    if (resolved_path == nullptr) {
        *out_result = -1;
        return true;
    }

#ifdef ESP_PLATFORM
    if (app_dir_set_cwd(resolved_path) == ERROR_NONE) {
        *out_result = 0;
    } else {
        errno = ENOENT;
        *out_result = -1;
    }
#else
    // The cwd is kept physical, like the kernel's own: ".." in the path follows symlinks
    char* canonical = realpath(resolved_path, nullptr);
    if (canonical == nullptr) {
        *out_result = -1;
        return true;
    }
    if (strlen(canonical) > FILE_MAX_PATH_LENGTH) {
        errno = ENAMETOOLONG;
        *out_result = -1;
    } else if (app_dir_set_cwd(canonical) == ERROR_NONE) {
        *out_result = 0;
    } else {
        errno = ENOTDIR;
        *out_result = -1;
    }
    free(canonical);
#endif
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
    if (sig < 0 || sig >= APP_LIBC_SIGNAL_COUNT || sig == SIGKILL || sig == SIGSTOP) {
        errno = EINVAL;
        *out_result = -1;
        return true;
    }
    const bool exists = pid > 0 && app_manager_get_state(static_cast<AppInstanceId>(pid)) != APP_INSTANCE_STATE_STOPPED;
    if (!exists) {
        errno = ESRCH;
        *out_result = -1;
        return true;
    }
    const error_t result = (sig == 0) ? ERROR_NONE : app_signal_send(static_cast<AppInstanceId>(pid), sig);
    switch (result) {
        case ERROR_NONE:
            *out_result = 0;
            break;
        case ERROR_NOT_FOUND:
            errno = ESRCH;
            *out_result = -1;
            break;
        default:
            errno = EAGAIN;
            *out_result = -1;
            break;
    }
    return true;
}

bool app_libc_try_getpid(int* out_result) {
    const AppInstanceId app_instance_id = app_scheduler_current_app_id();
    if (app_instance_id == 0) {
        return false;
    }
    *out_result = static_cast<int>(app_instance_id);
    return true;
}

bool app_libc_try_getppid(int* out_result) {
    const AppInstanceId app_instance_id = app_scheduler_current_app_id();
    if (app_instance_id == 0) {
        return false;
    }
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    const uint32_t parent_id = (iterator != ledger.instances.end()) ? iterator->second.parent_id : 0;
    mutex_unlock(&ledger.mutex);
    *out_result = static_cast<int>(parent_id);
    return true;
}

bool app_libc_try_usleep(unsigned long usec, int* out_result) {
    if (app_scheduler_current_app_id() == 0) {
        return false;
    }
    const auto ticks = static_cast<TickType_t>((static_cast<uint64_t>(usec) * configTICK_RATE_HZ + 999999) / 1000000);
    TickType_t remaining;
    if (sleep_unless_signalled(ticks, &remaining)) {
        errno = EINTR;
        *out_result = -1;
    } else {
        *out_result = 0;
    }
    return true;
}

bool app_libc_try_sleep(unsigned int seconds, unsigned int* out_result) {
    if (app_scheduler_current_app_id() == 0) {
        return false;
    }
    TickType_t remaining;
    sleep_unless_signalled(static_cast<TickType_t>(static_cast<uint64_t>(seconds) * configTICK_RATE_HZ), &remaining);
    *out_result = static_cast<unsigned int>(remaining / configTICK_RATE_HZ);
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
        if (app_signal_is_pending()) {
            errno = EINTR;
            *out_result = -1;
            return true;
        }
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
        const TickType_t wait_ticks = remaining < SIGNAL_CHECK_INTERVAL_TICKS ? remaining : SIGNAL_CHECK_INTERVAL_TICKS;
        if (first.events & POLLIN) {
            app_io_await(first.fd, APP_FILE_WAIT_READABLE, wait_ticks);
        } else if (first.events & POLLOUT) {
            app_io_await(first.fd, APP_FILE_WAIT_WRITABLE, wait_ticks);
        } else {
            delay_ticks(remaining < POLL_INTERVAL_TICKS ? remaining : POLL_INTERVAL_TICKS);
        }
    }
}

} // extern "C"
