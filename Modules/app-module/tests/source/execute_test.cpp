// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/event.h>
#include <app/execute.h>
#include <app/io.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/start.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <service/manager.h>

#include <tactility/delay.h>
#include <tactility/freertos/task.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

bool wait_for_state(uint32_t instance_id, AppInstanceState target, uint32_t timeout_ms) {
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (app_manager_get_state(instance_id) == target) {
            return true;
        }
        delay_millis(10);
        waited += 10;
    }
    return app_manager_get_state(instance_id) == target;
}

AppInstanceId topmost_instance_id() {
    AppInstanceId id = 0;
    return app_manager_get_topmost_instance_id(&id) == ERROR_NONE ? id : 0;
}

// Every APP_LOCATION_PATH-based test below would need its own fake path loader, registered
// under the same singleton APP_LOADER_PATH_SERVICE_ID that manager_test.cpp's own fake loader
// already claims - two independent-but-competing registrations in the same test binary is a real
// race (whichever file's static registrar runs first wins process-wide; the other file then runs
// silently against a loader it didn't write). Using APP_LOCATION_MEMORY + the real, already-safe
// app_internal_loader_service_manifest (registered via the find-instance-check idiom above, safe
// under this exact contention already used elsewhere in this test suite) sidesteps the problem
// entirely: no second competing registration exists.

// Subscribes until APP_EVENT_CLOSE like a real app instance; if launched with a single argv
// entry, returns it parsed as int immediately instead (the app_execute_for_result() shortcut,
// mirroring a modal dialog's result).
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

// Writes a fixed string to its own stdout then returns 7 as its result, for the
// app_execute_with_streams()/_for_result_with_streams() tests: proves a binding installed
// before the task starts actually reaches the app's own app_io_write() calls.
int32_t stream_writer_app_main(int, char*[]) {
    const char message[] = "loc";
    size_t sent = 0;
    while (sent < sizeof(message) - 1) {
        ssize_t written = app_io_write(STDOUT_FILENO, message + sent, sizeof(message) - 1 - sent);
        if (written < 0) {
            break;
        }
        sent += static_cast<size_t>(written);
    }
    return 7;
}

int32_t printf_stream_writer_app_main(int, char*[]) {
    printf("loc");
    return 7;
}

// Writes argv[0] to its own stdout, for the app_execute_with_streams() argv-delivery test.
int32_t argv_echo_app_main(int argc, char* argv[]) {
    if (argc < 1) {
        return -1;
    }
    const char* message = argv[0];
    size_t length = strlen(message);
    size_t sent = 0;
    while (sent < length) {
        ssize_t written = app_io_write(STDOUT_FILENO, message + sent, length - sent);
        if (written < 0) {
            break;
        }
        sent += static_cast<size_t>(written);
    }
    return 0;
}

} // namespace

TEST_CASE("app_execute runs a location with no manifest at all, and reports no topmost app id for it") {
    ensure_memory_loader_registered();

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) };
    uint32_t instance_id = 0;
    REQUIRE_EQ(app_execute(location, AppStackConfig {}, 0, nullptr, &instance_id), ERROR_NONE);
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    CHECK_EQ(topmost_instance_id(), instance_id);
    char buffer[64];
    // No manifest to report an id from - same NOT_FOUND a caller already sees for "nothing active".
    CHECK_EQ(app_manager_get_topmost_app_id(buffer, sizeof(buffer)), ERROR_NOT_FOUND);

    CHECK_EQ(app_manager_stop(instance_id), ERROR_NONE);
    CHECK_EQ(app_manager_get_state(instance_id), APP_INSTANCE_STATE_STOPPED);
}

TEST_CASE("app_execute doesn't disturb app_manager_get_topmost_app_id for a normal manifest-backed app started afterward") {
    ensure_memory_loader_registered();

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) };
    uint32_t unregistered_id = 0;
    REQUIRE_EQ(app_execute(location, AppStackConfig {}, 0, nullptr, &unregistered_id), ERROR_NONE);
    CHECK(wait_for_state(unregistered_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    AppManifest manifest { "test.app.execute.after", "After", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);
    uint32_t registered_id = 0;
    REQUIRE_EQ(app_start("test.app.execute.after", 0, nullptr, &registered_id), ERROR_NONE);
    CHECK(wait_for_state(registered_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    char buffer[64];
    CHECK_EQ(app_manager_get_topmost_app_id(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::string(buffer), "test.app.execute.after");

    app_manager_stop(unregistered_id);
    app_manager_stop(registered_id);
    app_manager_remove("test.app.execute.after");
}

TEST_CASE("app_execute_for_result delivers APP_EVENT_RESULT to the parent, with no manifest for the child either") {
    ensure_memory_loader_registered();

    AppManifest parent_manifest { "test.app.execute.parent", "Parent", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);

    uint32_t parent_id = 0;
    REQUIRE_EQ(app_start("test.app.execute.parent", 0, nullptr, &parent_id), ERROR_NONE);
    CHECK(wait_for_state(parent_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    TaskEventGroup parent_event_group {};
    task_event_group_construct(&parent_event_group);

    AppEventSubscription parent_sub {};
    REQUIRE_EQ(app_event_subscribe_with_app_id(&parent_sub, &parent_event_group, parent_id), ERROR_NONE);

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) };
    const char* argv[] = { "42" }; // location_app_main's single-arg shortcut - returns 42 immediately
    uint32_t child_id = 0;
    REQUIRE_EQ(app_execute_for_result(location, AppStackConfig {}, 1, argv, parent_id, &child_id), ERROR_NONE);

    REQUIRE_EQ(task_event_group_wait(&parent_event_group, parent_sub.bit, false, nullptr, pdMS_TO_TICKS(2000)), ERROR_NONE);
    AppEvent event {};
    REQUIRE_EQ(app_event_poll(&parent_sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, APP_EVENT_RESULT);
    CHECK_EQ(event.result.launch_id, child_id);
    CHECK_EQ(event.result.result, 42);

    app_event_unsubscribe(&parent_sub);
    task_event_group_destruct(&parent_event_group);
    app_manager_stop(child_id);
    app_manager_stop(parent_id);
    app_manager_remove("test.app.execute.parent");
}

TEST_CASE("app_execute_with_streams pipes a manifest-less child's app_io_write() calls into a parent-owned AppStream") {
    ensure_memory_loader_registered();

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(stream_writer_app_main) };

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[64];
    AppStream child_stdout {};
    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &event_group };

    AppInstanceId child_id = 0;
    REQUIRE_EQ(app_execute_with_streams(location, AppStackConfig {}, 0, nullptr, &binding, 1, &child_id), ERROR_NONE);

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

    REQUIRE(wait_for_state(child_id, APP_INSTANCE_STATE_STOPPED, 1000));
    app_stream_unsubscribe(&child_stdout);
    task_event_group_destruct(&event_group);
}

TEST_CASE("app_execute_with_streams passes argv through to the started app") {
    ensure_memory_loader_registered();

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(argv_echo_app_main) };

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[64];
    AppStream child_stdout {};
    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &event_group };

    const char* argv[] = { "hello" };
    AppInstanceId child_id = 0;
    REQUIRE_EQ(app_execute_with_streams(location, AppStackConfig {}, 1, argv, &binding, 1, &child_id), ERROR_NONE);

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
}

TEST_CASE("app_execute_for_result_with_streams delivers both the stream data and the APP_EVENT_RESULT") {
    ensure_memory_loader_registered();

    AppManifest parent_manifest { "test.app.execute.parent_streams", "Parent", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);

    uint32_t parent_id = 0;
    REQUIRE_EQ(app_start("test.app.execute.parent_streams", 0, nullptr, &parent_id), ERROR_NONE);
    CHECK(wait_for_state(parent_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    TaskEventGroup parent_event_group {};
    task_event_group_construct(&parent_event_group);
    AppEventSubscription parent_sub {};
    REQUIRE_EQ(app_event_subscribe_with_app_id(&parent_sub, &parent_event_group, parent_id), ERROR_NONE);

    uint8_t storage[64];
    AppStream child_stdout {};
    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &parent_event_group };

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(stream_writer_app_main) };
    uint32_t child_id = 0;
    REQUIRE_EQ(app_execute_for_result_with_streams(location, AppStackConfig {}, 0, nullptr, &binding, 1, parent_id, &child_id), ERROR_NONE);

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
    app_manager_remove("test.app.execute.parent_streams");
}

TEST_CASE("app_execute_for_result_with_streams pipes a child's plain printf() calls too") {
    ensure_memory_loader_registered();

    AppManifest parent_manifest { "test.app.execute.printf_parent", "Parent", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(location_app_main) } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);

    uint32_t parent_id = 0;
    REQUIRE_EQ(app_start("test.app.execute.printf_parent", 0, nullptr, &parent_id), ERROR_NONE);
    CHECK(wait_for_state(parent_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    TaskEventGroup parent_event_group {};
    task_event_group_construct(&parent_event_group);
    AppEventSubscription parent_sub {};
    REQUIRE_EQ(app_event_subscribe_with_app_id(&parent_sub, &parent_event_group, parent_id), ERROR_NONE);

    uint8_t storage[64];
    AppStream child_stdout {};
    AppStreamBinding binding { STDOUT_FILENO, &child_stdout, storage, sizeof(storage), &parent_event_group };

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(printf_stream_writer_app_main) };
    uint32_t child_id = 0;
    REQUIRE_EQ(app_execute_for_result_with_streams(location, AppStackConfig {}, 0, nullptr, &binding, 1, parent_id, &child_id), ERROR_NONE);

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
