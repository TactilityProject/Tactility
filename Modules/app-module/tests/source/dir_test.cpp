// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/dir.h>
#include <app/event.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <service/manager.h>

#include <tactility/delay.h>
#include <tactility/freertos/task.h>
#include <tactility/paths.h>

#include <atomic>
#include <cstring>

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

std::atomic<bool> results_ready { false };

bool result_default_is_root;
bool result_set_root_ok;
bool result_set_real_dir_ok;
bool result_get_after_set_matches;
bool result_set_relative_rejected;
bool result_set_missing_rejected;
bool result_cwd_unchanged_after_rejection;

// Runs entirely on the app's own task, matching env_test.cpp's own convention: every app_dir_*()
// call resolves against app_scheduler_current_app_id(), so this has to run on the instance's own
// task to test the real call path.
int32_t dir_test_app_main(int, char**) {
    char buf[FILE_MAX_PATH_STRING_LENGTH];

    result_default_is_root = app_dir_get_cwd(buf, sizeof(buf)) == ERROR_NONE && strcmp(buf, "/") == 0;

    result_set_root_ok = app_dir_set_cwd("/") == ERROR_NONE;

    result_set_real_dir_ok = app_dir_set_cwd("/tmp") == ERROR_NONE;
    result_get_after_set_matches = app_dir_get_cwd(buf, sizeof(buf)) == ERROR_NONE && strcmp(buf, "/tmp") == 0;

    result_set_relative_rejected = app_dir_set_cwd("relative") == ERROR_INVALID_ARGUMENT;
    result_set_missing_rejected = app_dir_set_cwd("/no/such/directory") == ERROR_NOT_FOUND;
    result_cwd_unchanged_after_rejection = app_dir_get_cwd(buf, sizeof(buf)) == ERROR_NONE && strcmp(buf, "/tmp") == 0;

    results_ready.store(true, std::memory_order_release);

    // Same subscribe-until-close contract as every other fake app in this test suite.
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
        if (done) break;
    }
    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
    return 0;
}

bool wait_for_results(uint32_t timeout_ms) {
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (results_ready.load(std::memory_order_acquire)) {
            return true;
        }
        delay_millis(10);
        waited += 10;
    }
    return results_ready.load(std::memory_order_acquire);
}

std::atomic<bool> child_result_ready { false };
bool result_child_inherited_parent_cwd;

// The child in the inheritance test below: checks what it sees, stashes the result, and returns
// immediately (no subscribe loop needed: nothing sends it APP_EVENT_CLOSE).
int32_t dir_child_main(int, char**) {
    char buf[FILE_MAX_PATH_STRING_LENGTH];
    result_child_inherited_parent_cwd = app_dir_get_cwd(buf, sizeof(buf)) == ERROR_NONE && strcmp(buf, "/tmp") == 0;
    child_result_ready.store(true, std::memory_order_release);
    return 0;
}

// The parent in the inheritance test below: `cd`s itself (app_dir_set_cwd(), mirroring what
// ShellFs::changeDirectory() does for a real `cd`), then starts a child that checks it inherited
// that cwd - the exact chain a coreutils app relies on to agree with the shell's own `pwd`.
int32_t dir_parent_main(int, char**) {
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    AppEventSubscription sub {};
    app_event_subscribe(&sub, &event_group);

    app_dir_set_cwd("/tmp");

    AppLocation child_location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(dir_child_main) };
    AppStartContext child_context = app_start_context_for_location(child_location);
    app_start_context_set_parent(&child_context, app_scheduler_current_app_id());
    uint32_t child_id = 0;
    app_start_with_context(&child_context, &child_id);

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
        if (done) break;
    }
    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
    return 0;
}

bool wait_for_child_result(uint32_t timeout_ms) {
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (child_result_ready.load(std::memory_order_acquire)) {
            return true;
        }
        delay_millis(10);
        waited += 10;
    }
    return child_result_ready.load(std::memory_order_acquire);
}

} // namespace

TEST_CASE("app_dir_* operate on the calling app instance's own cwd") {
    ensure_memory_loader_registered();

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(dir_test_app_main) };
    AppStartContext context = app_start_context_for_location(location);

    results_ready.store(false, std::memory_order_release);
    uint32_t instance_id = 0;
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));
    REQUIRE(wait_for_results(1000));

    CHECK(result_default_is_root);
    CHECK(result_set_root_ok);
    CHECK(result_set_real_dir_ok);
    CHECK(result_get_after_set_matches);
    CHECK(result_set_relative_rejected);
    CHECK(result_set_missing_rejected);
    CHECK(result_cwd_unchanged_after_rejection);

    app_manager_stop(instance_id);
}

TEST_CASE("app_dir_* report ERROR_NOT_FOUND when the calling task isn't a running app instance") {
    char buf[FILE_MAX_PATH_STRING_LENGTH];
    CHECK_EQ(app_dir_get_cwd(buf, sizeof(buf)), ERROR_NOT_FOUND);
    CHECK_EQ(app_dir_set_cwd("/tmp"), ERROR_NOT_FOUND);
}

TEST_CASE("a child started with a parent inherits the parent's cwd, set via app_dir_set_cwd") {
    ensure_memory_loader_registered();

    AppLocation parent_location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(dir_parent_main) };
    AppStartContext parent_context = app_start_context_for_location(parent_location);

    child_result_ready.store(false, std::memory_order_release);
    uint32_t parent_id = 0;
    REQUIRE_EQ(app_start_with_context(&parent_context, &parent_id), ERROR_NONE);
    CHECK(wait_for_state(parent_id, APP_INSTANCE_STATE_ACTIVE, 1000));
    REQUIRE(wait_for_child_result(1000));

    CHECK(result_child_inherited_parent_cwd);

    app_manager_stop(parent_id);
}
