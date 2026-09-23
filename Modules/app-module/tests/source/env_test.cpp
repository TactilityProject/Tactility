#include "doctest.h"

#include <app/env.h>
#include <app/event.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <service/manager.h>

#include <tactility/delay.h>
#include <tactility/freertos/task.h>

#include <atomic>
#include <cstring>

extern ServiceManifest app_internal_loader_service_manifest;

namespace {

// See manager_test.cpp's own copy of this helper for why this checks the registry directly
// rather than a per-translation-unit static bool, and why APP_LOCATION_MEMORY (the real,
// already-shared app_internal_loader_service_manifest) is used here instead of a second
// competing fake APP_LOCATION_PATH loader.
void ensure_memory_loader_registered() {
    if (service_manager_find_instance(APP_LOADER_MEMORY_SERVICE_ID) == nullptr) {
        service_manager_add(&app_internal_loader_service_manifest, /*auto_start=*/true);
    }
}

// Set by env_test_app_main() (below) once it has finished exercising app_env_*() - a test thread
// waits on this before reading the results_* variables, since reaching
// APP_INSTANCE_STATE_ACTIVE only means the instance's task has started, not that
// env_test_app_main() has gotten this far.
std::atomic<bool> results_ready { false };

bool result_get_before_set_is_null;
bool result_set_returns_ok;
bool result_get_after_set_matches;
bool result_no_overwrite_returns_ok;
bool result_no_overwrite_value_unchanged;
bool result_overwrite_returns_ok;
bool result_overwrite_value_changed;
bool result_put_returns_ok;
bool result_get_after_put_matches;
bool result_unset_returns_ok;
bool result_get_after_unset_is_null;
bool result_seeded_value_matches;

// Runs entirely on the app's own task, so app_scheduler_current_app_id() (which every
// app_env_*() call resolves against) actually names this instance - exercising the same call
// path a real app's own main() would use, not the test runner's task.
int32_t env_test_app_main(int, char**) {
    result_get_before_set_is_null = app_env_get("TEST_VAR") == nullptr;

    result_set_returns_ok = app_env_set("TEST_VAR", "one", true) == ERROR_NONE;
    const char* after_set = app_env_get("TEST_VAR");
    result_get_after_set_matches = after_set != nullptr && strcmp(after_set, "one") == 0;

    result_no_overwrite_returns_ok = app_env_set("TEST_VAR", "two", false) == ERROR_NONE;
    const char* after_no_overwrite = app_env_get("TEST_VAR");
    result_no_overwrite_value_unchanged = after_no_overwrite != nullptr && strcmp(after_no_overwrite, "one") == 0;

    result_overwrite_returns_ok = app_env_set("TEST_VAR", "three", true) == ERROR_NONE;
    const char* after_overwrite = app_env_get("TEST_VAR");
    result_overwrite_value_changed = after_overwrite != nullptr && strcmp(after_overwrite, "three") == 0;

    result_put_returns_ok = app_env_put("PUT_VAR=put_value") == ERROR_NONE;
    const char* after_put = app_env_get("PUT_VAR");
    result_get_after_put_matches = after_put != nullptr && strcmp(after_put, "put_value") == 0;

    result_unset_returns_ok = app_env_unset("TEST_VAR") == ERROR_NONE;
    result_get_after_unset_is_null = app_env_get("TEST_VAR") == nullptr;

    const char* seeded = app_env_get("SEEDED_VAR");
    result_seeded_value_matches = seeded != nullptr && strcmp(seeded, "seeded_value") == 0;

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

} // namespace

TEST_CASE("app_env_* operate on the calling app instance's own environment") {
    ensure_memory_loader_registered();

    AppLocation location { APP_LOCATION_MEMORY, reinterpret_cast<void*>(env_test_app_main) };
    AppStartContext context = app_start_context_for_location(location);
    const char* seeded_env[] = { "SEEDED_VAR=seeded_value", nullptr };
    app_start_context_set_environment(&context, const_cast<char**>(seeded_env));

    results_ready.store(false, std::memory_order_release);
    uint32_t instance_id = 0;
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));
    REQUIRE(wait_for_results(1000));

    CHECK(result_get_before_set_is_null);
    CHECK(result_set_returns_ok);
    CHECK(result_get_after_set_matches);
    CHECK(result_no_overwrite_returns_ok);
    CHECK(result_no_overwrite_value_unchanged);
    CHECK(result_overwrite_returns_ok);
    CHECK(result_overwrite_value_changed);
    CHECK(result_put_returns_ok);
    CHECK(result_get_after_put_matches);
    CHECK(result_unset_returns_ok);
    CHECK(result_get_after_unset_is_null);
    CHECK(result_seeded_value_matches);

    app_manager_stop(instance_id);
}

TEST_CASE("app_env_* reject invalid names/strings without touching any instance") {
    CHECK_EQ(app_env_set(nullptr, "value", true), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_env_set("", "value", true), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_env_set("NAME=X", "value", true), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_env_set("NAME", nullptr, true), ERROR_INVALID_ARGUMENT);

    CHECK_EQ(app_env_unset(nullptr), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_env_unset(""), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_env_unset("NAME=X"), ERROR_INVALID_ARGUMENT);

    CHECK_EQ(app_env_get(nullptr), nullptr);
    CHECK_EQ(app_env_get(""), nullptr);
    CHECK_EQ(app_env_get("NAME=X"), nullptr);

    CHECK_EQ(app_env_put(nullptr), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_env_put("NO_EQUALS_SIGN"), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(app_env_put("=NO_NAME"), ERROR_INVALID_ARGUMENT);
}

TEST_CASE("app_env_* report ERROR_NOT_FOUND when the calling task isn't a running app instance") {
    CHECK_EQ(app_env_set("NAME", "value", true), ERROR_NOT_FOUND);
    CHECK_EQ(app_env_unset("NAME"), ERROR_NOT_FOUND);
    CHECK_EQ(app_env_get("NAME"), nullptr);
}
