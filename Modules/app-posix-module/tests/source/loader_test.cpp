// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/event.h>
#include <app/execute.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/start.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <service/manager.h>

#include <tactility/delay.h>

#include <atomic>
#include <cstring>
#include <string>
#include <unistd.h>

extern ServiceManifest loader_service_manifest;              // app-posix-module's own
extern ServiceManifest app_internal_loader_service_manifest; // app-module's real memory loader

namespace {

void ensure_path_loader_registered() {
    if (service_manager_find_instance(APP_LOADER_PATH_SERVICE_ID) == nullptr) {
        service_manager_add(&loader_service_manifest, /*auto_start=*/true);
    }
}

void ensure_memory_loader_registered() {
    if (service_manager_find_instance(APP_LOADER_MEMORY_SERVICE_ID) == nullptr) {
        service_manager_add(&app_internal_loader_service_manifest, /*auto_start=*/true);
    }
}

std::string directory_of(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

const std::string FIXTURE_DIR = directory_of(FIXTURE_APP_PATH);

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

bool is_executable_path(const char* path) {
    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(path) };
    return app_is_executable(location);
}

std::atomic<int32_t> g_fixture_result { -1 };
std::atomic<bool> g_fixture_result_received { false };

// Starts the dlopen()ed fixture as its own modal child and stashes its returned result, so the
// test can inspect that result from the (in-process, directly readable) parent's own task.
int32_t parent_app_main(int, char*[]) {
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    app_event_subscribe(&sub, &event_group);

    AppInstanceId self_id = app_scheduler_current_app_id();

    AppManifest fixture_manifest {
        "test.posix.fixture", "Fixture", APP_CATEGORY_USER,
        { APP_LOCATION_PATH, const_cast<char*>(FIXTURE_APP_PATH) }
    };
    app_manager_add(&fixture_manifest);

    AppInstanceId fixture_id = 0;
    AppStartContext context = app_start_context_for_manifest(&fixture_manifest);
    app_start_context_set_parent(&context, self_id);
    app_start_with_context(&context, &fixture_id);

    while (true) {
        if (task_event_group_wait_any(&event_group, nullptr, pdMS_TO_TICKS(5000)) != ERROR_NONE) {
            break; // safety net so a bug here can't hang the test suite
        }
        AppEvent event {};
        bool got_result = false;
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_RESULT && event.result.launch_id == fixture_id) {
                g_fixture_result.store(event.result.result, std::memory_order_release);
                got_result = true;
            }
        }
        if (got_result) {
            break;
        }
    }
    g_fixture_result_received.store(true, std::memory_order_release);

    app_manager_remove("test.posix.fixture");
    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
    return 0;
}

std::string g_printf_fixture_output;
std::string g_printf_fixture_stderr;

// Starts the printf fixture with its stdout/stderr each piped through an AppStream, proving
// printf()/fprintf(stderr, ...) reach app_io_write() rather than bypassing straight to libc.
int32_t printf_parent_app_main(int, char*[]) {
    static uint8_t stdoutBuffer[256];
    static uint8_t stderrBuffer[256];
    AppStream stdoutStream {};
    AppStream stderrStream {};

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    app_event_subscribe(&sub, &event_group);

    AppInstanceId self_id = app_scheduler_current_app_id();

    AppStreamBinding bindings[] = {
        { STDOUT_FILENO, &stdoutStream, stdoutBuffer, sizeof(stdoutBuffer), &event_group },
        { STDERR_FILENO, &stderrStream, stderrBuffer, sizeof(stderrBuffer), &event_group },
    };

    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(PRINTF_FIXTURE_APP_PATH) };
    AppInstanceId childId = 0;
    AppStartContext context = app_start_context_for_location(location);
    app_start_context_set_streams(&context, bindings, 2);
    app_start_context_set_parent(&context, self_id);
    app_start_with_context(&context, &childId);

    uint8_t drain[256];
    bool childDone = false;
    while (!childDone) {
        if (task_event_group_wait_any(&event_group, nullptr, pdMS_TO_TICKS(5000)) != ERROR_NONE) {
            break; // safety net so a bug here can't hang the test suite
        }
        size_t n;
        while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
            g_printf_fixture_output.append(reinterpret_cast<const char*>(drain), n);
        }
        while ((n = app_stream_read(&stderrStream, drain, sizeof(drain))) > 0) {
            g_printf_fixture_stderr.append(reinterpret_cast<const char*>(drain), n);
        }
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_RESULT && event.result.launch_id == childId) {
                childDone = true;
            }
        }
    }
    size_t n;
    while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
        g_printf_fixture_output.append(reinterpret_cast<const char*>(drain), n);
    }
    while ((n = app_stream_read(&stderrStream, drain, sizeof(drain))) > 0) {
        g_printf_fixture_stderr.append(reinterpret_cast<const char*>(drain), n);
    }

    app_stream_unsubscribe(&stdoutStream);
    app_stream_unsubscribe(&stderrStream);
    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

TEST_CASE("app-posix-module's loader-path service dlopen()s a .so whose printf()/fprintf(stderr, ...) output reaches the parent's streams") {
    ensure_path_loader_registered();
    ensure_memory_loader_registered();
    g_printf_fixture_output.clear();
    g_printf_fixture_stderr.clear();

    AppManifest parent_manifest { "test.posix.printf_parent", "PrintfParent", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(printf_parent_app_main) } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);

    AppInstanceId parent_id = 0;
    AppStartContext parent_context;
    REQUIRE_EQ(app_start_context_from_id("test.posix.printf_parent", &parent_context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&parent_context, &parent_id), ERROR_NONE);
    REQUIRE(wait_for_state(parent_id, APP_INSTANCE_STATE_STOPPED, 3000));

    CHECK_EQ(g_printf_fixture_output, "hello from dlopen'd app\n");
    // Not exact: the child's own scheduler bookkeeping may also log to stderr while running.
    CHECK(g_printf_fixture_stderr.find("oops from dlopen'd app\n") != std::string::npos);

    app_manager_remove("test.posix.printf_parent");
}

TEST_CASE("app-posix-module's loader-path service dlopen()s a .so and calls its main(), which resolves a real Tactility symbol against the host") {
    ensure_path_loader_registered();
    ensure_memory_loader_registered();
    g_fixture_result.store(-1, std::memory_order_relaxed);
    g_fixture_result_received.store(false, std::memory_order_relaxed);

    AppManifest parent_manifest { "test.posix.parent", "Parent", APP_CATEGORY_USER, { APP_LOCATION_MEMORY, reinterpret_cast<void*>(parent_app_main) } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);

    AppInstanceId parent_id = 0;
    AppStartContext parent_context;
    REQUIRE_EQ(app_start_context_from_id("test.posix.parent", &parent_context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&parent_context, &parent_id), ERROR_NONE);
    REQUIRE(wait_for_state(parent_id, APP_INSTANCE_STATE_STOPPED, 3000));

    CHECK(g_fixture_result_received.load(std::memory_order_acquire));
    // A positive AppInstanceId proves the fixture's dlopen()ed main() actually resolved and
    // called app_scheduler_current_app_id() against the host process, not just "ran and returned
    // a hardcoded value" - 0 would mean it thought it wasn't running as an app instance at all.
    CHECK_GT(g_fixture_result.load(std::memory_order_acquire), 0);

    app_manager_remove("test.posix.parent");
}

TEST_CASE("app_is_executable() accepts a real .so") {
    ensure_path_loader_registered();

    CHECK(is_executable_path(FIXTURE_APP_PATH));
}

TEST_CASE("app_is_executable() rejects a .so-named file with no ELF header") {
    ensure_path_loader_registered();

    CHECK_FALSE(is_executable_path(FIXTURE_NON_ELF_PATH));
}

TEST_CASE("app_is_executable() rejects a nonexistent path") {
    ensure_path_loader_registered();

    CHECK_FALSE(is_executable_path((FIXTURE_DIR + "/does-not-exist.so").c_str()));
}

TEST_CASE("app_is_executable() rejects an install-directory-shaped path missing its per-arch .so") {
    ensure_path_loader_registered();

    // FIXTURE_DIR itself has no bin/posix-<arch>/app.so under it, so resolution fails.
    CHECK_FALSE(is_executable_path(FIXTURE_DIR.c_str()));
}

TEST_CASE("app_is_executable() accepts an install-directory-shaped path with bin/<arch>/app.so") {
    ensure_path_loader_registered();

    CHECK(is_executable_path(FIXTURE_INSTALL_DIR_PATH));
}
