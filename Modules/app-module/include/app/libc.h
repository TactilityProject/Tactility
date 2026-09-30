// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/poll.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat;
struct termios;

/**
 * App-instance behavior for libc calls, used by the platform modules' libc wraps
 * (app-esp32-module, app-posix-module).
 *
 * Each app_libc_try_*() function returns false when the call has nothing to do with an app instance
 * (not called from an app's task, or not an app fd), so the caller falls through to the real libc
 * function. When it returns true, the call was handled: *out_result holds the return value, and errno
 * is set on failure. A closed app fd fails with EBADF, so it never reaches a real fd of the same number.
 */

/** Handles ioctl(fd, TIOCGWINSZ, struct winsize*) for an app fd. */
bool app_libc_try_window_size(int fd, unsigned long request, void* arg, int* out_result);

/** @param[out] out_result @a buf on success, NULL on failure */
bool app_libc_try_getcwd(char* buf, size_t size, char** out_result);

bool app_libc_try_chdir(const char* path, int* out_result);

/** App fds report as character devices, which also makes isatty() true for them. */
bool app_libc_try_fstat(int fd, struct stat* st, int* out_result);

/**
 * App fd input is always raw and unechoed, and a written '\n' also returns the cursor.
 * The input flags are per app instance: ICRNL (on by default, like a terminal) makes stdin read '\r' as '\n'.
 */
bool app_libc_try_tcgetattr(int fd, struct termios* t, int* out_result);

/** Stores the input flags (c_iflag, of which ICRNL is applied). Other settings are accepted without effect. */
bool app_libc_try_tcsetattr(int fd, const struct termios* t, int* out_result);

/** Signal numbers an app can register a handler for are 1 to APP_LIBC_SIGNAL_COUNT - 1. */
#define APP_LIBC_SIGNAL_COUNT 32

typedef void (*AppLibcSignalHandler)(int sig);

/**
 * Records @a handler for @a sig in the calling app instance. Signals aren't delivered to apps yet,
 * so the handler is never called. SIGKILL, SIGSTOP and out-of-range signals fail with EINVAL.
 * @param[out] out_previous the previously registered handler, or SIG_ERR on failure
 */
bool app_libc_try_signal(int sig, AppLibcSignalHandler handler, AppLibcSignalHandler* out_previous);

/** Apps can't send signals: kill() from an app fails with ENOSYS rather than reaching the process. */
bool app_libc_try_kill(int pid, int sig, int* out_result);

typedef int (*AppLibcPollFunction)(struct pollfd* fds, nfds_t nfds, int timeout);

/**
 * Handles poll() when @a fds contains an app fd. Other fds are checked with @a real_poll.
 * Closed app fds report POLLNVAL.
 */
bool app_libc_try_poll(struct pollfd* fds, nfds_t nfds, int timeout, AppLibcPollFunction real_poll, int* out_result);

#ifdef __cplusplus
}
#endif
