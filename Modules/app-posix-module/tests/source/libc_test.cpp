// SPDX-License-Identifier: Apache-2.0
// libc calls made by an app instance (fstat, termios, poll, printf, exit, path-based calls), routed by this
// module's libc wraps (../source/stdio_wrap.cpp) to the app instance's own fds, cwd and lifecycle.
#include "doctest.h"

#include <app/event.h>
#include <app/io.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/signal.h>
#include <app/start.h>
#include <app/stream.h>

#include <service/manager.h>

#include <tactility/delay.h>
#include <tactility/paths.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern ServiceManifest app_internal_loader_service_manifest;
extern ServiceManifest loader_service_manifest;

namespace {

// See manager_test.cpp's own copy of this helper for why this checks the registry directly
// rather than a per-translation-unit static bool.
void ensure_memory_loader_registered() {
    if (service_manager_find_instance(APP_LOADER_MEMORY_SERVICE_ID) == nullptr) {
        service_manager_add(&app_internal_loader_service_manifest, /*auto_start=*/true);
    }
}

bool wait_for_state(AppInstanceId id, AppInstanceState target, uint32_t timeout_ms) {
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (app_manager_get_state(id) == target) {
            return true;
        }
        delay_millis(10);
        waited += 10;
    }
    return app_manager_get_state(id) == target;
}

bool wait_for_flag(std::atomic<bool>& flag, uint32_t timeout_ms) {
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (flag.load(std::memory_order_acquire)) {
            return true;
        }
        delay_millis(10);
        waited += 10;
    }
    return flag.load(std::memory_order_acquire);
}

int32_t location_app_main(int argc, char* argv[]) {
    if (argc == 1) {
        return static_cast<int32_t>(strtol(argv[0], nullptr, 10));
    }

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    app_event_subscribe(&sub, &event_group);

    while (true) {
        if (task_event_group_wait_any(&event_group, nullptr, pdMS_TO_TICKS(5000)) != ERROR_NONE) {
            break; // safety net so a bug here can't hang the test suite
        }
        bool done = false;
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                done = true;
                break;
            }
        }
        if (done) {
            break;
        }
    }

    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
    return 0;
}

int32_t printf_stream_writer_app_main(int, char*[]) {
    printf("loc");
    return 7;
}

std::atomic<bool> g_posix_stdin_is_char_device { false };
std::atomic<int> g_posix_tcgetattr_result { -2 };
std::atomic<bool> g_posix_stdin_is_raw { false };
std::atomic<int> g_posix_tcsetattr_result { -2 };
std::atomic<int> g_posix_poll_before_write { -2 };
std::atomic<int> g_posix_poll_after_write { -2 };
std::atomic<bool> g_posix_poll_first_done { false };
std::atomic<bool> g_posix_null_buffers_efault { false };

int32_t posix_calls_app_main(int, char*[]) {
    struct stat st {};
    g_posix_stdin_is_char_device.store(fstat(STDIN_FILENO, &st) == 0 && S_ISCHR(st.st_mode), std::memory_order_release);

    struct termios t {};
    g_posix_tcgetattr_result.store(tcgetattr(STDIN_FILENO, &t), std::memory_order_release);
    g_posix_stdin_is_raw.store((t.c_lflag & (ICANON | ECHO)) == 0, std::memory_order_release);
    g_posix_tcsetattr_result.store(tcsetattr(STDIN_FILENO, TCSANOW, &t), std::memory_order_release);

    struct stat* volatile null_stat = nullptr;
    struct termios* volatile null_termios = nullptr;
    const bool fstat_efault = fstat(STDIN_FILENO, null_stat) == -1 && errno == EFAULT;
    const bool tcgetattr_efault = tcgetattr(STDIN_FILENO, null_termios) == -1 && errno == EFAULT;
    g_posix_null_buffers_efault.store(fstat_efault && tcgetattr_efault, std::memory_order_release);

    struct pollfd fds { STDIN_FILENO, POLLIN, 0 };
    g_posix_poll_before_write.store(poll(&fds, 1, 50), std::memory_order_release);
    g_posix_poll_first_done.store(true, std::memory_order_release);
    g_posix_poll_after_write.store(poll(&fds, 1, 1000), std::memory_order_release);
    return 0;
}

std::atomic<bool> g_closed_fstat_ebadf { false };
std::atomic<bool> g_closed_tcgetattr_ebadf { false };
std::atomic<bool> g_closed_tcsetattr_ebadf { false };
std::atomic<bool> g_closed_ioctl_ebadf { false };
std::atomic<int> g_closed_poll_result { -2 };
std::atomic<int> g_closed_poll_revents { -2 };

// Must never reach the process's real fd 0 (the host terminal on the simulator).
int32_t closed_stdin_posix_calls_app_main(int, char*[]) {
    app_io_close(STDIN_FILENO);

    struct stat st {};
    g_closed_fstat_ebadf.store(fstat(STDIN_FILENO, &st) == -1 && errno == EBADF, std::memory_order_release);
    struct termios t {};
    g_closed_tcgetattr_ebadf.store(tcgetattr(STDIN_FILENO, &t) == -1 && errno == EBADF, std::memory_order_release);
    g_closed_tcsetattr_ebadf.store(tcsetattr(STDIN_FILENO, TCSANOW, &t) == -1 && errno == EBADF, std::memory_order_release);
    struct winsize ws {};
    g_closed_ioctl_ebadf.store(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 && errno == EBADF, std::memory_order_release);

    struct pollfd fds { STDIN_FILENO, POLLIN, 0 };
    g_closed_poll_result.store(poll(&fds, 1, 1000), std::memory_order_release);
    g_closed_poll_revents.store(fds.revents, std::memory_order_release);
    return 0;
}

std::atomic<bool> g_exit_before { false };
std::atomic<bool> g_exit_after { false };

int32_t exit_app_main(int, char*[]) {
    g_exit_before.store(true, std::memory_order_release);
    exit(42);
    g_exit_after.store(true, std::memory_order_release);
    return 0;
}

void test_signal_handler(int) {
}

std::atomic<bool> g_signal_first_default { false };
std::atomic<bool> g_signal_returns_previous { false };
std::atomic<bool> g_signal_sigkill_rejected { false };
std::atomic<bool> g_kill_sigstop_rejected { false };

int32_t signal_app_main(int, char*[]) {
    g_signal_first_default.store(signal(SIGWINCH, test_signal_handler) == SIG_DFL, std::memory_order_release);
    g_signal_returns_previous.store(signal(SIGWINCH, SIG_IGN) == test_signal_handler, std::memory_order_release);
    g_signal_sigkill_rejected.store(signal(SIGKILL, test_signal_handler) == SIG_ERR && errno == EINVAL, std::memory_order_release);
    // Would stop the whole test process if it reached the real kill()
    g_kill_sigstop_rejected.store(kill(getpid(), SIGSTOP) == -1 && errno == EINVAL, std::memory_order_release);
    return 0;
}

std::atomic<bool> g_icrnl_default_on { false };
std::atomic<int> g_icrnl_first_read { -2 };
std::atomic<bool> g_icrnl_first_done { false };
std::atomic<int> g_icrnl_second_read { -2 };

int32_t icrnl_app_main(int, char*[]) {
    struct termios t {};
    tcgetattr(STDIN_FILENO, &t);
    g_icrnl_default_on.store((t.c_iflag & ICRNL) != 0, std::memory_order_release);

    char c = 0;
    g_icrnl_first_read.store(read(STDIN_FILENO, &c, 1) == 1 ? c : -1, std::memory_order_release);

    t.c_iflag &= ~ICRNL;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    g_icrnl_first_done.store(true, std::memory_order_release);
    g_icrnl_second_read.store(read(STDIN_FILENO, &c, 1) == 1 ? c : -1, std::memory_order_release);
    return 0;
}

// Set by the app below: whether a relative path opened by code built into the simulator resolved against the process's cwd
std::atomic<int> g_builtin_relative_open { -1 };
const char* g_builtin_relative_path = nullptr;

int32_t builtin_relative_path_app_main(int, char*[]) {
    FILE* file = fopen(g_builtin_relative_path, "r");
    g_builtin_relative_open.store(file != nullptr ? 1 : 0, std::memory_order_release);
    if (file != nullptr) {
        fclose(file);
    }
    return 0;
}

} // namespace

TEST_CASE("ICRNL is on by default and makes stdin read the Enter key's \\r as \\n, until an app clears it") {
    ensure_memory_loader_registered();
    g_icrnl_default_on.store(false, std::memory_order_relaxed);
    g_icrnl_first_read.store(-2, std::memory_order_relaxed);
    g_icrnl_first_done.store(false, std::memory_order_relaxed);
    g_icrnl_second_read.store(-2, std::memory_order_relaxed);

    AppManifest manifest { "test.libc.icrnl", "Icrnl", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(icrnl_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[16];
    AppStream child_stdin {};
    AppStreamBinding binding { STDIN_FILENO, &child_stdin, storage, sizeof(storage), &event_group, {}, -1 };
    AppInstanceId child_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.libc.icrnl", &context), ERROR_NONE);
    app_start_context_set_streams(&context, &binding, 1);
    REQUIRE_EQ(app_start_with_context(&context, &child_id), ERROR_NONE);

    app_stream_write(&child_stdin, "\r", 1);
    REQUIRE(wait_for_flag(g_icrnl_first_done, 1000));
    app_stream_write(&child_stdin, "\r", 1);

    REQUIRE(wait_for_state(child_id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK(g_icrnl_default_on.load(std::memory_order_acquire));
    CHECK_EQ(g_icrnl_first_read.load(std::memory_order_acquire), '\n');
    CHECK_EQ(g_icrnl_second_read.load(std::memory_order_acquire), '\r');

    app_stream_unsubscribe(&child_stdin);
    task_event_group_destruct(&event_group);
    app_manager_remove("test.libc.icrnl");
}

TEST_CASE("signal() in an app records handlers per app instance, and kill() never reaches the process") {
    ensure_memory_loader_registered();
    g_signal_first_default.store(false, std::memory_order_relaxed);
    g_signal_returns_previous.store(false, std::memory_order_relaxed);
    g_signal_sigkill_rejected.store(false, std::memory_order_relaxed);
    g_kill_sigstop_rejected.store(false, std::memory_order_relaxed);

    AppManifest manifest { "test.libc.signal", "Signal", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(signal_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.libc.signal", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 2000));

    CHECK(g_signal_first_default.load(std::memory_order_acquire));
    CHECK(g_signal_returns_previous.load(std::memory_order_acquire));
    CHECK(g_signal_sigkill_rejected.load(std::memory_order_acquire));
    CHECK(g_kill_sigstop_rejected.load(std::memory_order_acquire));

    app_manager_remove("test.libc.signal");
}

TEST_CASE("exit() in an app ends only that app instance") {
    ensure_memory_loader_registered();
    g_exit_before.store(false, std::memory_order_relaxed);
    g_exit_after.store(false, std::memory_order_relaxed);

    AppManifest manifest { "test.io.exit", "Exit", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(exit_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.io.exit", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 2000));

    CHECK(g_exit_before.load(std::memory_order_acquire));
    CHECK_FALSE(g_exit_after.load(std::memory_order_acquire));

    app_manager_remove("test.io.exit");
}

TEST_CASE("fstat, ioctl, termios and poll on a closed app stdin fail with EBADF/POLLNVAL instead of reaching the real fd") {
    ensure_memory_loader_registered();
    g_closed_fstat_ebadf.store(false, std::memory_order_relaxed);
    g_closed_tcgetattr_ebadf.store(false, std::memory_order_relaxed);
    g_closed_tcsetattr_ebadf.store(false, std::memory_order_relaxed);
    g_closed_ioctl_ebadf.store(false, std::memory_order_relaxed);
    g_closed_poll_result.store(-2, std::memory_order_relaxed);
    g_closed_poll_revents.store(-2, std::memory_order_relaxed);

    AppManifest manifest { "test.io.closed_posix_calls", "ClosedPosixCalls", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(closed_stdin_posix_calls_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.io.closed_posix_calls", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 2000));

    CHECK(g_closed_fstat_ebadf.load(std::memory_order_acquire));
    CHECK(g_closed_tcgetattr_ebadf.load(std::memory_order_acquire));
    CHECK(g_closed_tcsetattr_ebadf.load(std::memory_order_acquire));
    CHECK(g_closed_ioctl_ebadf.load(std::memory_order_acquire));
    CHECK_EQ(g_closed_poll_result.load(std::memory_order_acquire), 1);
    CHECK_EQ(g_closed_poll_revents.load(std::memory_order_acquire), POLLNVAL);

    app_manager_remove("test.io.closed_posix_calls");
}

TEST_CASE("fstat, termios and poll on a bound app stdin report a raw character device that becomes readable") {
    ensure_memory_loader_registered();
    g_posix_stdin_is_char_device.store(false, std::memory_order_relaxed);
    g_posix_tcgetattr_result.store(-2, std::memory_order_relaxed);
    g_posix_stdin_is_raw.store(false, std::memory_order_relaxed);
    g_posix_tcsetattr_result.store(-2, std::memory_order_relaxed);
    g_posix_poll_before_write.store(-2, std::memory_order_relaxed);
    g_posix_poll_after_write.store(-2, std::memory_order_relaxed);
    g_posix_poll_first_done.store(false, std::memory_order_relaxed);
    g_posix_null_buffers_efault.store(false, std::memory_order_relaxed);

    AppManifest manifest { "test.io.posix_calls", "PosixCalls", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(posix_calls_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[16];
    AppStream child_stdin {};
    AppStreamBinding binding { STDIN_FILENO, &child_stdin, storage, sizeof(storage), &event_group, {}, -1 };
    AppInstanceId child_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.io.posix_calls", &context), ERROR_NONE);
    app_start_context_set_streams(&context, &binding, 1);
    REQUIRE_EQ(app_start_with_context(&context, &child_id), ERROR_NONE);

    REQUIRE(wait_for_flag(g_posix_poll_first_done, 1000));
    app_stream_write(&child_stdin, "x", 1);

    REQUIRE(wait_for_state(child_id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK(g_posix_stdin_is_char_device.load(std::memory_order_acquire));
    CHECK_EQ(g_posix_tcgetattr_result.load(std::memory_order_acquire), 0);
    CHECK(g_posix_stdin_is_raw.load(std::memory_order_acquire));
    CHECK_EQ(g_posix_tcsetattr_result.load(std::memory_order_acquire), 0);
    CHECK(g_posix_null_buffers_efault.load(std::memory_order_acquire));
    CHECK_EQ(g_posix_poll_before_write.load(std::memory_order_acquire), 0);
    CHECK_EQ(g_posix_poll_after_write.load(std::memory_order_acquire), 1);

    app_stream_unsubscribe(&child_stdin);
    task_event_group_destruct(&event_group);
    app_manager_remove("test.io.posix_calls");
}

TEST_CASE("app_execute_for_result_with_streams pipes a child's plain printf() calls too") {
    ensure_memory_loader_registered();

    AppManifest parent_manifest { "test.app.execute.printf_parent", "Parent", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);

    uint32_t parent_id = 0;
    AppStartContext parent_context;
    REQUIRE_EQ(app_start_context_from_id("test.app.execute.printf_parent", &parent_context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&parent_context, &parent_id), ERROR_NONE);
    CHECK(wait_for_state(parent_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    TaskEventGroup parent_event_group {};
    task_event_group_construct(&parent_event_group);
    AppEventSubscription parent_sub {};
    REQUIRE_EQ(app_event_subscribe_with_app_id(&parent_sub, &parent_event_group, parent_id), ERROR_NONE);

    uint8_t storage[64];
    AppStream child_stdout {};
    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &parent_event_group, {}, -1 };

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(printf_stream_writer_app_main) };
    uint32_t child_id = 0;
    AppStartContext context = app_start_context_for_location(location);
    app_start_context_set_streams(&context, &binding, 1);
    app_start_context_set_parent(&context, parent_id);
    REQUIRE_EQ(app_start_with_context(&context, &child_id), ERROR_NONE);

    std::vector<uint8_t> received;
    while (app_stream_await(&child_stdout, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(1000)) == ERROR_NONE) {
        uint8_t chunk[16];
        size_t n = app_stream_read(&child_stdout, chunk, sizeof(chunk));
        if (n == 0) {
            break; // EOF
        }
        received.insert(received.end(), chunk, chunk + n);
    }
    REQUIRE_EQ(received.size(), 3u);
    CHECK_EQ(std::memcmp(received.data(), "loc", 3), 0);

    REQUIRE_EQ(task_event_group_wait(&parent_event_group, parent_sub.bit, false, nullptr, pdMS_TO_TICKS(2000)), ERROR_NONE);
    AppEvent event {};
    REQUIRE_EQ(app_event_poll(&parent_sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, APP_EVENT_RESULT);
    CHECK_EQ(event.result.launch_id, child_id);
    CHECK_EQ(event.result.result, 7);

    app_stream_unsubscribe(&child_stdout);
    app_event_unsubscribe(&parent_sub);
    task_event_group_destruct(&parent_event_group);
    app_manager_stop(child_id);
    app_manager_stop(parent_id);
    app_manager_remove("test.app.execute.printf_parent");
}

namespace {

std::atomic<int> g_signal_handled { 0 };
std::atomic<bool> g_signal_app_blocked { false };
std::atomic<int> g_signal_call_result { -2 };
std::atomic<int> g_signal_call_errno { 0 };
std::atomic<bool> g_signal_app_continued { false };

void record_signal_handler(int sig) {
    g_signal_handled.store(sig, std::memory_order_release);
}

void reset_signal_flags() {
    g_signal_handled.store(0, std::memory_order_relaxed);
    g_signal_app_blocked.store(false, std::memory_order_relaxed);
    g_signal_call_result.store(-2, std::memory_order_relaxed);
    g_signal_call_errno.store(0, std::memory_order_relaxed);
    g_signal_app_continued.store(false, std::memory_order_relaxed);
}

int32_t handled_signal_app_main(int, char*[]) {
    signal(SIGUSR1, record_signal_handler);
    g_signal_app_blocked.store(true, std::memory_order_release);
    char c;
    const ssize_t result = read(STDIN_FILENO, &c, 1);
    g_signal_call_errno.store(errno, std::memory_order_release);
    g_signal_call_result.store(static_cast<int>(result), std::memory_order_release);
    return 0;
}

int32_t default_signal_app_main(int, char*[]) {
    g_signal_app_blocked.store(true, std::memory_order_release);
    char c;
    read(STDIN_FILENO, &c, 1);
    g_signal_app_continued.store(true, std::memory_order_release);
    return 0;
}

int32_t ignored_signal_app_main(int, char*[]) {
    signal(SIGHUP, SIG_IGN);
    g_signal_app_blocked.store(true, std::memory_order_release);
    g_signal_call_result.store(usleep(300 * 1000), std::memory_order_release);
    g_signal_app_continued.store(true, std::memory_order_release);
    return 0;
}

int32_t default_sleep_app_main(int, char*[]) {
    g_signal_app_blocked.store(true, std::memory_order_release);
    g_signal_call_result.store(usleep(300 * 1000), std::memory_order_release);
    g_signal_app_continued.store(true, std::memory_order_release);
    return 0;
}

int32_t interrupted_sleep_app_main(int, char*[]) {
    signal(SIGUSR1, record_signal_handler);
    g_signal_app_blocked.store(true, std::memory_order_release);
    const int result = usleep(10 * 1000 * 1000);
    g_signal_call_errno.store(errno, std::memory_order_release);
    g_signal_call_result.store(result, std::memory_order_release);
    return 0;
}

int32_t interrupted_poll_app_main(int, char*[]) {
    signal(SIGUSR1, record_signal_handler);
    g_signal_app_blocked.store(true, std::memory_order_release);
    struct pollfd fd { STDIN_FILENO, POLLIN, 0 };
    const int result = poll(&fd, 1, -1);
    g_signal_call_errno.store(errno, std::memory_order_release);
    g_signal_call_result.store(result, std::memory_order_release);
    return 0;
}

std::atomic<bool> g_kill_probe_ok { false };
std::atomic<bool> g_kill_missing_esrch { false };
std::atomic<bool> g_kill_group_esrch { false };
std::atomic<bool> g_getppid_top_level { false };
std::atomic<bool> g_kill_self_handled_before_return { false };

int32_t kill_app_main(int, char*[]) {
    signal(SIGUSR2, record_signal_handler);
    g_kill_probe_ok.store(kill(getpid(), 0) == 0, std::memory_order_release);
    g_kill_missing_esrch.store(kill(0x7FFFFFF0, SIGTERM) == -1 && errno == ESRCH, std::memory_order_release);
    g_kill_group_esrch.store(kill(0, SIGTERM) == -1 && errno == ESRCH, std::memory_order_release);
    g_getppid_top_level.store(getppid() == 0, std::memory_order_release);
    kill(getpid(), SIGUSR2);
    g_kill_self_handled_before_return.store(g_signal_handled.load(std::memory_order_acquire) == SIGUSR2, std::memory_order_release);
    return 0;
}

std::atomic<int> g_event_signal { 0 };
std::atomic<bool> g_event_closed { false };

int32_t evented_signal_app_main(int, char*[]) {
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    AppEventSubscription sub {};
    app_event_subscribe(&sub, &event_group);
    g_signal_app_blocked.store(true, std::memory_order_release);
    bool closed = false;
    while (!closed) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_SIGNAL) {
                g_event_signal.store(event.signal.sig, std::memory_order_release);
            } else if (event.type == APP_EVENT_CLOSE) {
                closed = true;
            }
        }
    }
    g_event_closed.store(true, std::memory_order_release);
    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
    return 0;
}

/** Runs @a app_main with a stdin that is never written to, so a read() of it blocks. */
struct BlockedStdinApp {
    TaskEventGroup event_group {};
    uint8_t storage[16] {};
    AppStream stdin_stream {};
    AppInstanceId id = 0;
    const char* manifest_id;
    AppManifest manifest { "", "Signal", APP_CATEGORY_USER, {} };

    BlockedStdinApp(const char* manifestId, int32_t (*app_main)(int, char*[])) : manifest_id(manifestId) {
        ensure_memory_loader_registered();
        reset_signal_flags();
        strncpy(manifest.id, manifestId, sizeof(manifest.id) - 1);
        manifest.location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(app_main) };
        REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);
        task_event_group_construct(&event_group);
        AppStreamBinding binding { STDIN_FILENO, &stdin_stream, storage, sizeof(storage), &event_group, {}, -1 };
        AppStartContext context;
        REQUIRE_EQ(app_start_context_from_id(manifest_id, &context), ERROR_NONE);
        app_start_context_set_streams(&context, &binding, 1);
        REQUIRE_EQ(app_start_with_context(&context, &id), ERROR_NONE);
        REQUIRE(wait_for_flag(g_signal_app_blocked, 1000));
        // Lets the app reach the blocking call after raising its flag
        delay_millis(50);
    }

    ~BlockedStdinApp() {
        app_manager_stop(id);
        app_stream_unsubscribe(&stdin_stream);
        task_event_group_destruct(&event_group);
        app_manager_remove(manifest_id);
    }
};

} // namespace

TEST_CASE("A signal with a handler interrupts a blocked read() with EINTR and calls the handler") {
    BlockedStdinApp app("test.libc.signal_handled", handled_signal_app_main);

    CHECK_EQ(app_signal_send(app.id, SIGUSR1), ERROR_NONE);

    REQUIRE(wait_for_state(app.id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK_EQ(g_signal_handled.load(std::memory_order_acquire), SIGUSR1);
    CHECK_EQ(g_signal_call_result.load(std::memory_order_acquire), -1);
    CHECK_EQ(g_signal_call_errno.load(std::memory_order_acquire), EINTR);
}

TEST_CASE("A signal without a handler ends an app blocked in read()") {
    BlockedStdinApp app("test.libc.signal_default", default_signal_app_main);

    CHECK_EQ(app_signal_send(app.id, SIGHUP), ERROR_NONE);

    REQUIRE(wait_for_state(app.id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK_FALSE(g_signal_app_continued.load(std::memory_order_acquire));
}

TEST_CASE("app_manager_stop() ends an app without an event subscription via SIGTERM") {
    BlockedStdinApp app("test.libc.signal_stop", default_signal_app_main);

    CHECK_EQ(app_manager_stop(app.id), ERROR_NONE);

    CHECK_EQ(app_manager_get_state(app.id), APP_INSTANCE_STATE_STOPPED);
    CHECK_FALSE(g_signal_app_continued.load(std::memory_order_acquire));
}

TEST_CASE("An ignored signal does not interrupt the app") {
    BlockedStdinApp app("test.libc.signal_ignored", ignored_signal_app_main);

    CHECK_EQ(app_signal_send(app.id, SIGHUP), ERROR_NONE);

    REQUIRE(wait_for_state(app.id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK(g_signal_app_continued.load(std::memory_order_acquire));
    CHECK_EQ(g_signal_call_result.load(std::memory_order_acquire), 0);
}

TEST_CASE("SIGCONT and the stop signals without a handler neither interrupt nor end the app") {
    BlockedStdinApp app("test.libc.signal_job_control", default_sleep_app_main);

    CHECK_EQ(app_signal_send(app.id, SIGCONT), ERROR_NONE);
    CHECK_EQ(app_signal_send(app.id, SIGTSTP), ERROR_NONE);
    CHECK_EQ(app_signal_send(app.id, SIGTTIN), ERROR_NONE);
    CHECK_EQ(app_signal_send(app.id, SIGTTOU), ERROR_NONE);

    REQUIRE(wait_for_state(app.id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK(g_signal_app_continued.load(std::memory_order_acquire));
    CHECK_EQ(g_signal_call_result.load(std::memory_order_acquire), 0);
}

TEST_CASE("A signal interrupts usleep() with EINTR") {
    BlockedStdinApp app("test.libc.signal_sleep", interrupted_sleep_app_main);

    CHECK_EQ(app_signal_send(app.id, SIGUSR1), ERROR_NONE);

    REQUIRE(wait_for_state(app.id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK_EQ(g_signal_handled.load(std::memory_order_acquire), SIGUSR1);
    CHECK_EQ(g_signal_call_result.load(std::memory_order_acquire), -1);
    CHECK_EQ(g_signal_call_errno.load(std::memory_order_acquire), EINTR);
}

TEST_CASE("A signal interrupts poll() without a timeout with EINTR") {
    BlockedStdinApp app("test.libc.signal_poll", interrupted_poll_app_main);

    CHECK_EQ(app_signal_send(app.id, SIGUSR1), ERROR_NONE);

    REQUIRE(wait_for_state(app.id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK_EQ(g_signal_handled.load(std::memory_order_acquire), SIGUSR1);
    CHECK_EQ(g_signal_call_result.load(std::memory_order_acquire), -1);
    CHECK_EQ(g_signal_call_errno.load(std::memory_order_acquire), EINTR);
}

TEST_CASE("An app with an event subscription receives signals as events") {
    g_event_signal.store(0, std::memory_order_relaxed);
    g_event_closed.store(false, std::memory_order_relaxed);
    BlockedStdinApp app("test.libc.signal_evented", evented_signal_app_main);

    CHECK_EQ(app_signal_send(app.id, SIGUSR1), ERROR_NONE);
    uint32_t waited = 0;
    while (g_event_signal.load(std::memory_order_acquire) == 0 && waited < 1000) {
        delay_millis(10);
        waited += 10;
    }
    CHECK_EQ(g_event_signal.load(std::memory_order_acquire), SIGUSR1);
    CHECK_FALSE(g_event_closed.load(std::memory_order_acquire));

    CHECK_EQ(app_signal_send(app.id, SIGTERM), ERROR_NONE);
    REQUIRE(wait_for_state(app.id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK(g_event_closed.load(std::memory_order_acquire));
}

TEST_CASE("kill() and getpid() address app instances") {
    ensure_memory_loader_registered();
    reset_signal_flags();
    g_kill_probe_ok.store(false, std::memory_order_relaxed);
    g_kill_missing_esrch.store(false, std::memory_order_relaxed);
    g_kill_group_esrch.store(false, std::memory_order_relaxed);
    g_getppid_top_level.store(false, std::memory_order_relaxed);
    g_kill_self_handled_before_return.store(false, std::memory_order_relaxed);

    AppManifest manifest { "test.libc.kill", "Kill", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(kill_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.libc.kill", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 2000));

    CHECK(g_kill_probe_ok.load(std::memory_order_acquire));
    CHECK(g_kill_missing_esrch.load(std::memory_order_acquire));
    CHECK(g_kill_group_esrch.load(std::memory_order_acquire));
    CHECK(g_getppid_top_level.load(std::memory_order_acquire));
    CHECK(g_kill_self_handled_before_return.load(std::memory_order_acquire));

    app_manager_remove("test.libc.kill");
}

TEST_CASE("app_signal_send() rejects an out-of-range signal and an unknown app") {
    CHECK_EQ(app_signal_send(1, 0), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_signal_send(1, 32), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_signal_send(0x7FFFFFF0, SIGTERM), ERROR_NOT_FOUND);
}

TEST_CASE("Path-based calls of an app binary resolve relative paths against the app's own cwd") {
    if (service_manager_find_instance(APP_LOADER_PATH_SERVICE_ID) == nullptr) {
        service_manager_add(&loader_service_manifest, /*auto_start=*/true);
    }
    char directory_template[] = "/tmp/tactility-paths-XXXXXX";
    REQUIRE(mkdtemp(directory_template) != nullptr);
    const std::string result_path = std::string(directory_template) + ".result";

    const char* argv[] = { PATHS_FIXTURE_APP_PATH, directory_template, result_path.c_str() };
    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(PATHS_FIXTURE_APP_PATH) };
    AppStartContext context = app_start_context_for_location(location);
    app_start_context_set_arguments_ext(&context, 3, argv);
    AppInstanceId instance_id = 0;
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 2000));

    int failed_check = -1;
    FILE* result = fopen(result_path.c_str(), "r");
    REQUIRE_NE(result, nullptr);
    CHECK_EQ(fscanf(result, "%d", &failed_check), 1);
    fclose(result);
    CHECK_EQ(failed_check, 0);

    const std::string kept_path = std::string(directory_template) + "/kept.txt";
    CHECK_EQ(access(kept_path.c_str(), F_OK), 0);
    // Outside an app, relative paths still resolve against the process's own cwd
    CHECK_NE(access("kept.txt", F_OK), 0);

    unlink(kept_path.c_str());
    unlink(result_path.c_str());
    rmdir(directory_template);
}

TEST_CASE("Path-based calls of code built into the simulator keep resolving against the process's cwd") {
    ensure_memory_loader_registered();
    char path_template[] = "tactility-builtin-path-XXXXXX";
    const int fd = mkstemp(path_template);
    REQUIRE_NE(fd, -1);
    close(fd);
    g_builtin_relative_path = path_template;
    g_builtin_relative_open.store(-1, std::memory_order_relaxed);

    AppManifest manifest { "test.libc.builtin_paths", "Paths", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(builtin_relative_path_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);
    AppInstanceId instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.libc.builtin_paths", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 2000));

    CHECK_EQ(g_builtin_relative_open.load(std::memory_order_acquire), 1);

    unlink(path_template);
    app_manager_remove("test.libc.builtin_paths");
}
