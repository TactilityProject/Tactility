// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/io.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/start.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <service/manager.h>

#include <tactility/delay.h>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

extern ServiceManifest app_internal_loader_service_manifest;

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

std::atomic<ssize_t> g_stdio_write_result { -2 };
std::atomic<ssize_t> g_stdio_read_result { -2 };

int32_t unbound_stdio_app_main(int, char*[]) {
    g_stdio_write_result.store(app_io_write(STDOUT_FILENO, "x", 1), std::memory_order_release);
    uint8_t buffer[1];
    g_stdio_read_result.store(app_io_read(STDIN_FILENO, buffer, sizeof(buffer)), std::memory_order_release);
    return 0;
}

int32_t stdout_writer_app_main(int, char*[]) {
    const char message[] = "hello";
    size_t sent = 0;
    while (sent < sizeof(message) - 1) {
        ssize_t written = app_io_write(STDOUT_FILENO, message + sent, sizeof(message) - 1 - sent);
        if (written < 0) {
            break;
        }
        sent += static_cast<size_t>(written);
    }
    return 0;
}

int32_t stdout_printf_app_main(int, char*[]) {
    printf("hello");
    return 0;
}

std::atomic<bool> g_blocked_writer_saw_error { false };
std::atomic<bool> g_blocked_writer_done { false };

int32_t blocked_writer_app_main(int, char*[]) {
    const char message[] = "0123456789"; // larger than the test's 4-byte stream capacity
    size_t sent = 0;
    while (sent < sizeof(message) - 1) {
        ssize_t written = app_io_write(STDOUT_FILENO, message + sent, sizeof(message) - 1 - sent);
        if (written < 0) {
            g_blocked_writer_saw_error.store(true, std::memory_order_release);
            break;
        }
        sent += static_cast<size_t>(written);
    }
    g_blocked_writer_done.store(true, std::memory_order_release);
    return 0;
}

std::atomic<error_t> g_await_before_write { ERROR_NONE };
std::atomic<error_t> g_await_after_write { ERROR_TIMEOUT };
std::atomic<bool> g_await_done { false };

int32_t await_reader_app_main(int, char*[]) {
    g_await_before_write.store(app_io_await(STDIN_FILENO, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(50)), std::memory_order_release);
    g_await_after_write.store(app_io_await(STDIN_FILENO, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(1000)), std::memory_order_release);
    g_await_done.store(true, std::memory_order_release);
    return 0;
}

std::atomic<ssize_t> g_real_file_write_result { -2 };
std::atomic<ssize_t> g_real_file_read_result { -2 };
std::atomic<bool> g_real_file_read_matches { false };
std::atomic<int> g_real_file_close_result { -2 };

// A real file fd from a bare open() call: app-module never intercepts open(), so this fd is
// never bound/allocated in the app's own fd table. app_io_read/write/close() must still pass it
// straight through to the real syscall instead of treating it as an unknown app-level fd.
int32_t real_file_io_app_main(int, char*[]) {
    const char* path = "/tmp/tactility_app_io_passthrough_test.txt";
    int real_fd = ::open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (real_fd < 0) {
        return 0;
    }

    g_real_file_write_result.store(app_io_write(real_fd, "hi", 2), std::memory_order_release);
    ::lseek(real_fd, 0, SEEK_SET);
    char buffer[2] = {};
    ssize_t read_result = app_io_read(real_fd, buffer, sizeof(buffer));
    g_real_file_read_result.store(read_result, std::memory_order_release);
    g_real_file_read_matches.store(read_result == 2 && buffer[0] == 'h' && buffer[1] == 'i', std::memory_order_release);
    g_real_file_close_result.store(app_io_close(real_fd), std::memory_order_release);

    ::unlink(path);
    return 0;
}

struct SelfBoundObject {
    char written[16] = {};
    size_t written_len = 0;
};

ssize_t self_bound_write(void* object, const void* buffer, size_t size) {
    auto* obj = static_cast<SelfBoundObject*>(object);
    size_t n = size < sizeof(obj->written) ? size : sizeof(obj->written);
    memcpy(obj->written, buffer, n);
    obj->written_len = n;
    return static_cast<ssize_t>(n);
}

ssize_t self_bound_read(void*, void* buffer, size_t size) {
    const char reply[] = "yo";
    size_t n = size < sizeof(reply) - 1 ? size : sizeof(reply) - 1;
    memcpy(buffer, reply, n);
    return static_cast<ssize_t>(n);
}

error_t self_bound_close(void*) { return ERROR_NONE; }
error_t self_bound_await(void*, AppFileWait, TickType_t) { return ERROR_NONE; }
uint32_t self_bound_poll(void*) { return APP_FILE_READABLE | APP_FILE_WRITABLE; }

const AppFileOps SELF_BOUND_OPS = {
    .read = self_bound_read,
    .write = self_bound_write,
    .close = self_bound_close,
    .await = self_bound_await,
    .poll = self_bound_poll,
    .retain = nullptr,
    .release = nullptr,
};

SelfBoundObject g_self_bound_object;
std::atomic<error_t> g_self_bind_result { ERROR_UNDEFINED };
std::atomic<ssize_t> g_self_bound_write_result { -2 };
std::atomic<ssize_t> g_self_bound_read_result { -2 };
char g_self_bound_read_buffer[16] = {};

int32_t self_bind_app_main(int, char*[]) {
    g_self_bind_result.store(app_io_bind_self(STDOUT_FILENO, &SELF_BOUND_OPS, &g_self_bound_object), std::memory_order_release);
    g_self_bound_write_result.store(app_io_write(STDOUT_FILENO, "hi", 2), std::memory_order_release);
    g_self_bound_read_result.store(app_io_read(STDOUT_FILENO, g_self_bound_read_buffer, sizeof(g_self_bound_read_buffer) - 1), std::memory_order_release);
    return 0;
}

std::atomic<int> g_double_close_first_result { -2 };
std::atomic<int> g_double_close_second_result { -2 };
std::atomic<ssize_t> g_write_after_close_result { -2 };

// A second close() of an already-closed app fd, and a write() after that, must both report
// EBADF, never fall through to the platform syscall, which by then could be operating on a real
// fd that fd number was recycled for (e.g. the process's real stdout).
int32_t double_close_app_main(int, char*[]) {
    g_double_close_first_result.store(app_io_close(STDOUT_FILENO), std::memory_order_release);
    g_double_close_second_result.store(app_io_close(STDOUT_FILENO), std::memory_order_release);
    g_write_after_close_result.store(app_io_write(STDOUT_FILENO, "x", 1), std::memory_order_release);
    return 0;
}

} // namespace

TEST_CASE("an app's stdio fds default to the null device: write succeeds and discards, read reports EOF") {
    ensure_memory_loader_registered();
    g_stdio_write_result.store(-2, std::memory_order_relaxed);
    g_stdio_read_result.store(-2, std::memory_order_relaxed);

    AppManifest manifest { "test.io.unbound", "Unbound", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(unbound_stdio_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    REQUIRE_EQ(app_start("test.io.unbound", 0, nullptr, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 1000));

    CHECK_EQ(g_stdio_write_result.load(std::memory_order_acquire), 1);
    CHECK_EQ(g_stdio_read_result.load(std::memory_order_acquire), 0);

    app_manager_remove("test.io.unbound");
}

TEST_CASE("app_start_with_streams pipes a child's app_io_write() calls into a parent-owned AppStream, EOF at exit") {
    ensure_memory_loader_registered();

    AppManifest manifest { "test.io.writer", "Writer", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(stdout_writer_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[64];
    AppStream child_stdout {};

    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &event_group };
    AppInstanceId child_id = 0;
    REQUIRE_EQ(app_start_with_streams("test.io.writer", &binding, 1, &child_id), ERROR_NONE);

    std::vector<uint8_t> received;
    while (app_stream_await(&child_stdout, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(1000)) == ERROR_NONE) {
        uint8_t chunk[16];
        size_t n = app_stream_read(&child_stdout, chunk, sizeof(chunk));
        if (n == 0) {
            break; // EOF
        }
        received.insert(received.end(), chunk, chunk + n);
    }

    REQUIRE_EQ(received.size(), 5u);
    CHECK_EQ(std::memcmp(received.data(), "hello", 5), 0);

    REQUIRE(wait_for_state(child_id, APP_INSTANCE_STATE_STOPPED, 1000));
    app_stream_unsubscribe(&child_stdout);
    task_event_group_destruct(&event_group);
    app_manager_remove("test.io.writer");
}

TEST_CASE("app_io_await blocks until the bound stream becomes readable or times out") {
    ensure_memory_loader_registered();
    g_await_before_write.store(ERROR_NONE, std::memory_order_relaxed);
    g_await_after_write.store(ERROR_TIMEOUT, std::memory_order_relaxed);
    g_await_done.store(false, std::memory_order_relaxed);

    AppManifest manifest { "test.io.await", "Await", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(await_reader_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[16];
    AppStream child_stdin {};
    AppStreamBinding binding { STDIN_FILENO, &child_stdin, storage, sizeof(storage), &event_group };
    AppInstanceId child_id = 0;
    REQUIRE_EQ(app_start_with_streams("test.io.await", &binding, 1, &child_id), ERROR_NONE);

    // The child's first await (50ms, on an empty stream) has time to time out before anything is
    // written here.
    delay_millis(150);
    CHECK_FALSE(g_await_done.load(std::memory_order_acquire));

    app_stream_write(&child_stdin, "x", 1);

    REQUIRE(wait_for_state(child_id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK_EQ(g_await_before_write.load(std::memory_order_acquire), ERROR_TIMEOUT);
    CHECK_EQ(g_await_after_write.load(std::memory_order_acquire), ERROR_NONE);

    app_stream_unsubscribe(&child_stdin);
    task_event_group_destruct(&event_group);
    app_manager_remove("test.io.await");
}

TEST_CASE("a write blocked on a full stream wakes with an error once the consumer closes it") {
    ensure_memory_loader_registered();
    g_blocked_writer_saw_error.store(false, std::memory_order_relaxed);
    g_blocked_writer_done.store(false, std::memory_order_relaxed);

    AppManifest manifest { "test.io.blocked", "Blocked", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(blocked_writer_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[4]; // smaller than the 10 bytes blocked_writer_app_main sends
    AppStream child_stdout {};

    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &event_group };
    AppInstanceId child_id = 0;
    REQUIRE_EQ(app_start_with_streams("test.io.blocked", &binding, 1, &child_id), ERROR_NONE);

    // Never drained: the child fills the 4-byte buffer and blocks awaiting space for the rest.
    delay_millis(200);
    CHECK_FALSE(g_blocked_writer_done.load(std::memory_order_acquire));

    // Unblocks the writer without touching child_stdout's sync primitives, safe even while it
    // may still be blocked in app_stream_await() (unlike app_stream_unsubscribe()).
    app_stream_close(&child_stdout);

    REQUIRE(wait_for_state(child_id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK(g_blocked_writer_saw_error.load(std::memory_order_acquire));

    // Only safe now that the child's task (the only other party that could be blocked on this
    // stream) has fully exited.
    app_stream_unsubscribe(&child_stdout);
    task_event_group_destruct(&event_group);
    app_manager_remove("test.io.blocked");
}

TEST_CASE("app_stream_unsubscribe is safe to call while a write is actively blocked") {
    ensure_memory_loader_registered();
    g_blocked_writer_saw_error.store(false, std::memory_order_relaxed);
    g_blocked_writer_done.store(false, std::memory_order_relaxed);

    AppManifest manifest { "test.io.unsub_race", "UnsubRace", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(blocked_writer_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[4]; // smaller than the 10 bytes blocked_writer_app_main sends
    AppStream child_stdout {};

    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &event_group };
    AppInstanceId child_id = 0;
    REQUIRE_EQ(app_start_with_streams("test.io.unsub_race", &binding, 1, &child_id), ERROR_NONE);

    // Give the child time to fill the 4-byte buffer and block inside app_io_write(), already
    // dispatched through app_fd_table_get_and_retain() and currently waiting in
    // app_stream_await(). This is the exact state app_stream_unsubscribe() must be safe to run
    // against, with no prior app_stream_close() or wait for the child to stop first.
    delay_millis(200);
    REQUIRE_FALSE(g_blocked_writer_done.load(std::memory_order_acquire));

    // Regression: unsubscribing directly here used to be able to destruct
    // stream->mutex while the blocked write above was still executing against it.
    REQUIRE_EQ(app_stream_unsubscribe(&child_stdout), ERROR_NONE);

    REQUIRE(wait_for_state(child_id, APP_INSTANCE_STATE_STOPPED, 1000));
    CHECK(g_blocked_writer_saw_error.load(std::memory_order_acquire));

    task_event_group_destruct(&event_group);
    app_manager_remove("test.io.unsub_race");
}

TEST_CASE("app_io_read/write/close pass through a real file fd app-module never bound") {
    ensure_memory_loader_registered();
    g_real_file_write_result.store(-2, std::memory_order_relaxed);
    g_real_file_read_result.store(-2, std::memory_order_relaxed);
    g_real_file_read_matches.store(false, std::memory_order_relaxed);
    g_real_file_close_result.store(-2, std::memory_order_relaxed);

    AppManifest manifest { "test.io.real_file", "RealFile", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(real_file_io_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    REQUIRE_EQ(app_start("test.io.real_file", 0, nullptr, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 1000));

    CHECK_EQ(g_real_file_write_result.load(std::memory_order_acquire), 2);
    CHECK_EQ(g_real_file_read_result.load(std::memory_order_acquire), 2);
    CHECK(g_real_file_read_matches.load(std::memory_order_acquire));
    CHECK_EQ(g_real_file_close_result.load(std::memory_order_acquire), 0);

    app_manager_remove("test.io.real_file");
}

TEST_CASE("app_io_bind_self installs a custom AppFileOps used by subsequent app_io_read/write") {
    ensure_memory_loader_registered();
    g_self_bind_result.store(ERROR_UNDEFINED, std::memory_order_relaxed);
    g_self_bound_write_result.store(-2, std::memory_order_relaxed);
    g_self_bound_read_result.store(-2, std::memory_order_relaxed);
    g_self_bound_object = SelfBoundObject {};
    memset(g_self_bound_read_buffer, 0, sizeof(g_self_bound_read_buffer));

    AppManifest manifest { "test.io.self_bind", "SelfBind", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(self_bind_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    REQUIRE_EQ(app_start("test.io.self_bind", 0, nullptr, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 1000));

    CHECK_EQ(g_self_bind_result.load(std::memory_order_acquire), ERROR_NONE);
    CHECK_EQ(g_self_bound_write_result.load(std::memory_order_acquire), 2);
    CHECK_EQ(g_self_bound_object.written_len, 2u);
    CHECK_EQ(std::memcmp(g_self_bound_object.written, "hi", 2), 0);
    CHECK_EQ(g_self_bound_read_result.load(std::memory_order_acquire), 2);
    CHECK_EQ(std::memcmp(g_self_bound_read_buffer, "yo", 2), 0);

    app_manager_remove("test.io.self_bind");
}

TEST_CASE("app_io_bind_self returns ERROR_NOT_FOUND when called outside a running app instance") {
    SelfBoundObject object;
    CHECK_EQ(app_io_bind_self(STDOUT_FILENO, &SELF_BOUND_OPS, &object), ERROR_NOT_FOUND);
}

TEST_CASE("closing an already-closed app fd reports EBADF instead of falling through to the platform") {
    ensure_memory_loader_registered();
    g_double_close_first_result.store(-2, std::memory_order_relaxed);
    g_double_close_second_result.store(-2, std::memory_order_relaxed);
    g_write_after_close_result.store(-2, std::memory_order_relaxed);

    AppManifest manifest { "test.io.double_close", "DoubleClose", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(double_close_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    AppInstanceId instance_id = 0;
    REQUIRE_EQ(app_start("test.io.double_close", 0, nullptr, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_STOPPED, 1000));

    CHECK_EQ(g_double_close_first_result.load(std::memory_order_acquire), 0);
    CHECK_EQ(g_double_close_second_result.load(std::memory_order_acquire), -1);
    CHECK_EQ(g_write_after_close_result.load(std::memory_order_acquire), -1);

    app_manager_remove("test.io.double_close");
}
