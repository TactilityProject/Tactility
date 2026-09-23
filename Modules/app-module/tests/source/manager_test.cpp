#include "doctest.h"

#include <app/event.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/start.h>
#include <app/scheduler.h>

#include <service/manager.h>

#include <tactility/delay.h>
#include <tactility/freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern ServiceManifest app_internal_loader_service_manifest;

namespace {

error_t fake_load(AppLocation, void** out_runtime) {
    *out_runtime = nullptr;
    return ERROR_NONE;
}

// Stashed by fake_run() on every call, for tests that need to verify exactly what argc/argv it
// received (e.g. that app-module deep-copied the caller's argv) without a getter to query it
// through.
int last_received_argc = -1;
std::vector<std::string> last_received_argv;

// fake_run() runs on the app's own task; stash_received_arguments() writes
// last_received_argc/last_received_argv there while a test thread reads them - wait_for_state()
// only establishes that the instance reached APP_INSTANCE_STATE_ACTIVE (set before
// AppLoaderApi::run() is even called, i.e. before fake_run() runs at all), not that
// stash_received_arguments() has finished writing. This flag is the actual ordering: reset
// before starting the app, set (release) as the last step of stash_received_arguments(), waited
// on (acquire) before a test reads the stashed values.
std::atomic<bool> arguments_stashed { false };

void stash_received_arguments(int argc, char* argv[]) {
    last_received_argc = argc;
    last_received_argv.clear();
    for (int i = 0; i < argc; i++) {
        last_received_argv.emplace_back(argv[i]);
    }
    arguments_stashed.store(true, std::memory_order_release);
}

// A minimal stand-in for a real app's main(): subscribes to its own app_event stream and exits
// as soon as it's asked to close - exactly the contract every app instance (with its own
// dedicated task for its whole lifetime) is expected to follow. If launched with a single
// parameter (app_start_for_result()), acts as a modal dialog instead: returns the
// requested result (argv[0], parsed as an int) immediately (the app's own return value IS the
// delivered APP_EVENT_RESULT.result - see app_scheduler.cpp's thread_main()).
int32_t fake_run(void*, uint32_t /*app_instance_id*/, int argc, char* argv[]) {
    stash_received_arguments(argc, argv);

    if (argc == 1) {
        // Single-arg shortcut used by the start_for_result() result-delivery tests: returns
        // immediately with argv[0] parsed as the result code, instead of running the normal
        // event loop below. Tests that pass other argc (0, or >1 to check deep-copy) fall
        // through and run the loop as usual.
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
        if (done) break;
    }

    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
    return 0;
}

void fake_unload(void*) {
}

AppLoaderApi fake_loader_api = {
    .load = fake_load,
    .run = fake_run,
    .unload = fake_unload,
};

void* create_loader_service(const ServiceManifest*) {
    return &fake_loader_api;
}

void destroy_loader_service(const ServiceManifest*, void*) {
}

ServiceManifest fake_loader_manifest = {
    .id = APP_LOADER_PATH_SERVICE_ID,
    .create_service = create_loader_service,
    .destroy_service = destroy_loader_service,
    .on_start = nullptr,
    .on_stop = nullptr,
};

// Checks the registry directly rather than a per-translation-unit static bool, matching
// ensure_memory_loader_registered() below - this is the only file that registers a fake path
// loader, but a second one would silently win the race otherwise (see execute_test.cpp, which
// deliberately avoids needing one at all for exactly this reason).
void ensure_fake_loader_registered() {
    if (service_manager_find_instance(APP_LOADER_PATH_SERVICE_ID) == nullptr) {
        service_manager_add(&fake_loader_manifest, /*auto_start=*/true);
    }
}

// app-module's real APP_LOCATION_MEMORY loader (source/app_internal_loader.cpp) - not a fake,
// since it has no platform dependency and is exactly what a statically-linked app would go
// through. Checks the registry directly rather than a per-translation-unit static bool: other
// test files (stream_test.cpp, io_test.cpp) register the same manifest the same way, and
// doctest doesn't guarantee which file's tests run first.
void ensure_memory_loader_registered() {
    if (service_manager_find_instance(APP_LOADER_MEMORY_SERVICE_ID) == nullptr) {
        service_manager_add(&app_internal_loader_service_manifest, /*auto_start=*/true);
    }
}

// Same subscribe-until-close contract as fake_run() above, but called directly as an AppMainFn -
// this is what a real internal app's entry point looks like.
int32_t fake_app_main(int argc, char* argv[]) {
    return fake_run(nullptr, app_scheduler_current_app_id(), argc, argv);
}

// Wraps app_manager_get_topmost_instance_id() for terse assertions: 0 if no app is Active.
AppInstanceId topmost_instance_id() {
    AppInstanceId id = 0;
    return app_manager_get_topmost_instance_id(&id) == ERROR_NONE ? id : 0;
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

bool wait_for_arguments_stashed(uint32_t timeout_ms) {
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (arguments_stashed.load(std::memory_order_acquire)) {
            return true;
        }
        delay_millis(10);
        waited += 10;
    }
    return arguments_stashed.load(std::memory_order_acquire);
}

} // namespace

TEST_CASE("app_start activates an app instance, app_manager_stop terminates it") {
    ensure_fake_loader_registered();

    AppManifest manifest { "test.app.a", "Test App A", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    uint32_t instance_id = 0;
    AppStartContext context_a;
    REQUIRE_EQ(app_start_context_from_id("test.app.a", &context_a), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context_a, &instance_id), ERROR_NONE);
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    CHECK_EQ(app_manager_stop(instance_id), ERROR_NONE);
    CHECK_EQ(app_manager_get_state(instance_id), APP_INSTANCE_STATE_STOPPED);

    app_manager_remove("test.app.a");
}

TEST_CASE("app_start never touches another already-running app - every instance gets its own task") {
    ensure_fake_loader_registered();

    AppManifest manifest_b { "test.app.b", "Test App B", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    AppManifest manifest_c { "test.app.c", "Test App C", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest_b), ERROR_NONE);
    REQUIRE_EQ(app_manager_add(&manifest_c), ERROR_NONE);

    uint32_t id_b = 0;
    AppStartContext context_b;
    REQUIRE_EQ(app_start_context_from_id("test.app.b", &context_b), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context_b, &id_b), ERROR_NONE);
    CHECK(wait_for_state(id_b, APP_INSTANCE_STATE_ACTIVE, 1000));

    uint32_t id_c = 0;
    AppStartContext context_c;
    REQUIRE_EQ(app_start_context_from_id("test.app.c", &context_c), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context_c, &id_c), ERROR_NONE);
    CHECK(wait_for_state(id_c, APP_INSTANCE_STATE_ACTIVE, 1000));

    // b is untouched by c starting - both stay Active at once, each with its own task.
    CHECK_EQ(app_manager_get_state(id_b), APP_INSTANCE_STATE_ACTIVE);

    app_manager_stop(id_b);
    app_manager_stop(id_c);
    app_manager_remove("test.app.b");
    app_manager_remove("test.app.c");
}

TEST_CASE("app_start always creates a fresh instance, even for the same manifest id twice") {
    ensure_fake_loader_registered();

    AppManifest manifest { "test.app.twice", "Test App Twice", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    uint32_t id_first = 0;
    AppStartContext context_first;
    REQUIRE_EQ(app_start_context_from_id("test.app.twice", &context_first), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context_first, &id_first), ERROR_NONE);
    CHECK(wait_for_state(id_first, APP_INSTANCE_STATE_ACTIVE, 1000));

    uint32_t id_second = 0;
    AppStartContext context_second;
    REQUIRE_EQ(app_start_context_from_id("test.app.twice", &context_second), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context_second, &id_second), ERROR_NONE);
    CHECK(wait_for_state(id_second, APP_INSTANCE_STATE_ACTIVE, 1000));

    CHECK_NE(id_first, id_second);
    CHECK_EQ(app_manager_get_state(id_first), APP_INSTANCE_STATE_ACTIVE);

    app_manager_stop(id_first);
    app_manager_stop(id_second);
    app_manager_remove("test.app.twice");
}

TEST_CASE("app_manager_get_state returns STOPPED for an unknown instance id") {
    CHECK_EQ(app_manager_get_state(999999), APP_INSTANCE_STATE_STOPPED);
}

TEST_CASE("app_start_with_parameters deep-copies argv before the app instance receives it") {
    ensure_fake_loader_registered();

    AppManifest manifest { "test.app.args", "Test App Args", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    uint32_t instance_id = 0;
    arguments_stashed.store(false, std::memory_order_relaxed);
    {
        // Caller's argv is stack-local and goes out of scope immediately after this block -
        // proves app-module made its own copy rather than aliasing the caller's strings.
        std::string ssid = "MyNetwork";
        std::string password = "hunter2";
        const char* argv[] = { ssid.c_str(), password.c_str() };
        AppStartContext context;
        REQUIRE_EQ(app_start_context_from_id("test.app.args", &context), ERROR_NONE);
        app_start_context_set_arguments_ext(&context, 2, argv);
        REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    }
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));
    REQUIRE(wait_for_arguments_stashed(1000));

    REQUIRE_EQ(last_received_argc, 2);
    REQUIRE_EQ(last_received_argv.size(), 2u);
    CHECK_EQ(last_received_argv[0], "MyNetwork");
    CHECK_EQ(last_received_argv[1], "hunter2");

    app_manager_stop(instance_id);
    app_manager_remove("test.app.args");
}

TEST_CASE("app_manager_add rejects a duplicate id") {
    AppManifest manifest { "test.app.dup", "Test App Dup", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);
    CHECK_EQ(app_manager_add(&manifest), ERROR_INVALID_ARGUMENT);
    app_manager_remove("test.app.dup");
}

TEST_CASE("app_manager_for_each_manifest visits every registered manifest, including newly added ones") {
    AppManifest manifest_x { "test.app.foreach.x", "X", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    AppManifest manifest_y { "test.app.foreach.y", "Y", APP_CATEGORY_SETTINGS, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest_x), ERROR_NONE);
    REQUIRE_EQ(app_manager_add(&manifest_y), ERROR_NONE);

    std::vector<std::string> seen_ids;
    app_manager_for_each_manifest([](const AppManifest* manifest, void* context) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(manifest->id);
    }, &seen_ids);

    CHECK(std::ranges::find(seen_ids, "test.app.foreach.x") != seen_ids.end());
    CHECK(std::ranges::find(seen_ids, "test.app.foreach.y") != seen_ids.end());

    app_manager_remove("test.app.foreach.x");
    app_manager_remove("test.app.foreach.y");

    seen_ids.clear();
    app_manager_for_each_manifest([](const AppManifest* manifest, void* context) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(manifest->id);
    }, &seen_ids);
    CHECK(std::ranges::find(seen_ids, "test.app.foreach.x") == seen_ids.end());
}

TEST_CASE("app_start fails for an unregistered manifest id") {
    uint32_t instance_id = 0;
    AppStartContext context;
    CHECK_EQ(app_start_context_from_id("test.app.nonexistent", &context), ERROR_NOT_FOUND);
}

TEST_CASE("app_start runs an APP_LOCATION_MEMORY app via its function pointer, through the real internal loader") {
    ensure_memory_loader_registered();

    AppManifest manifest {
        "test.app.memory",
        "Test App Memory",
        APP_CATEGORY_USER,
        { APP_LOCATION_MEMORY, reinterpret_cast<void*>(fake_app_main) }
    };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    uint32_t instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.app.memory", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    CHECK_EQ(app_manager_stop(instance_id), ERROR_NONE);
    CHECK_EQ(app_manager_get_state(instance_id), APP_INSTANCE_STATE_STOPPED);

    app_manager_remove("test.app.memory");
}

TEST_CASE("app_start_for_result delivers APP_EVENT_RESULT to the parent, which stays Active throughout") {
    ensure_fake_loader_registered();

    AppManifest parent_manifest { "test.app.parent", "Parent", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    AppManifest child_manifest { "test.app.child", "Child", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);
    REQUIRE_EQ(app_manager_add(&child_manifest), ERROR_NONE);

    uint32_t parent_id = 0;
    AppStartContext parent_context;
    REQUIRE_EQ(app_start_context_from_id("test.app.parent", &parent_context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&parent_context, &parent_id), ERROR_NONE);
    CHECK(wait_for_state(parent_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    TaskEventGroup parent_event_group {};
    task_event_group_construct(&parent_event_group);

    AppEventSubscription parent_sub {};
    REQUIRE_EQ(app_event_subscribe_with_app_id(&parent_sub, &parent_event_group, parent_id), ERROR_NONE);

    const char* argv[] = { "42" };
    uint32_t child_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.app.child", &context), ERROR_NONE);
    app_start_context_set_arguments_ext(&context, 1, argv);
    app_start_context_set_parent(&context, parent_id);
    REQUIRE_EQ(app_start_with_context(&context, &child_id), ERROR_NONE);

    // Launching a modal child never touches the parent's own task/state.
    CHECK_EQ(app_manager_get_state(parent_id), APP_INSTANCE_STATE_ACTIVE);

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
    app_manager_remove("test.app.parent");
    app_manager_remove("test.app.child");
}

TEST_CASE("app_start_for_result delivers the child's own return value as the result") {
    ensure_fake_loader_registered();

    AppManifest parent_manifest { "test.app.parent2", "Parent2", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    AppManifest child_manifest { "test.app.child2", "Child2", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&parent_manifest), ERROR_NONE);
    REQUIRE_EQ(app_manager_add(&child_manifest), ERROR_NONE);

    uint32_t parent_id = 0;
    AppStartContext parent_context;
    REQUIRE_EQ(app_start_context_from_id("test.app.parent2", &parent_context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&parent_context, &parent_id), ERROR_NONE);
    CHECK(wait_for_state(parent_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    TaskEventGroup parent_event_group {};
    task_event_group_construct(&parent_event_group);

    AppEventSubscription parent_sub {};
    REQUIRE_EQ(app_event_subscribe_with_app_id(&parent_sub, &parent_event_group, parent_id), ERROR_NONE);

    uint32_t child_id = 0;
    // No parameters - fake_run falls through to its normal CLOSE loop instead of acting as a
    // dialog.
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.app.child2", &context), ERROR_NONE);
    app_start_context_set_parent(&context, parent_id);
    REQUIRE_EQ(app_start_with_context(&context, &child_id), ERROR_NONE);
    CHECK(wait_for_state(child_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    app_manager_stop(child_id); // force-close

    REQUIRE_EQ(task_event_group_wait(&parent_event_group, parent_sub.bit, false, nullptr, pdMS_TO_TICKS(2000)), ERROR_NONE);
    AppEvent event {};
    REQUIRE_EQ(app_event_poll(&parent_sub, &event), ERROR_NONE);
    CHECK_EQ(event.type, APP_EVENT_RESULT);
    CHECK_EQ(event.result.launch_id, child_id);
    CHECK_EQ(event.result.result, 0); // fake_run's CLOSE loop always returns 0

    app_event_unsubscribe(&parent_sub);
    task_event_group_destruct(&parent_event_group);
    app_manager_stop(parent_id);
    app_manager_remove("test.app.parent2");
    app_manager_remove("test.app.child2");
}

TEST_CASE("app_manager_get_topmost_instance_id returns NOT_FOUND when nothing is active, then tracks who's on top") {
    ensure_fake_loader_registered();
    AppInstanceId id = 999999;
    CHECK_EQ(app_manager_get_topmost_instance_id(&id), ERROR_NOT_FOUND);

    AppManifest manifest_a { "test.app.top_a", "A", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    AppManifest manifest_b { "test.app.top_b", "B", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest_a), ERROR_NONE);
    REQUIRE_EQ(app_manager_add(&manifest_b), ERROR_NONE);

    uint32_t id_a = 0;
    AppStartContext context_a;
    REQUIRE_EQ(app_start_context_from_id("test.app.top_a", &context_a), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context_a, &id_a), ERROR_NONE);
    CHECK(wait_for_state(id_a, APP_INSTANCE_STATE_ACTIVE, 1000));
    CHECK_EQ(topmost_instance_id(), id_a);

    // a stays Active - b just has a higher (more recently allocated) instance id, so it becomes
    // topmost without a superseding/saving.
    uint32_t id_b = 0;
    AppStartContext context_b;
    REQUIRE_EQ(app_start_context_from_id("test.app.top_b", &context_b), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context_b, &id_b), ERROR_NONE);
    CHECK(wait_for_state(id_b, APP_INSTANCE_STATE_ACTIVE, 1000));
    CHECK_EQ(topmost_instance_id(), id_b);

    char app_id_buffer[64];
    REQUIRE_EQ(app_manager_get_topmost_app_id(app_id_buffer, sizeof(app_id_buffer)), ERROR_NONE);
    CHECK_EQ(std::string(app_id_buffer), "test.app.top_b");

    // A modal child stays Active alongside its parent while shown - the child (started more
    // recently) must be reported as topmost, not the parent. No parameters, so fake_run() takes
    // its persistent CLOSE loop branch instead of instantly resolving like a real dialog would -
    // needed here so there's a reliable window to observe it as topmost.
    uint32_t id_c = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.app.top_a", &context), ERROR_NONE);
    app_start_context_set_parent(&context, id_b);
    REQUIRE_EQ(app_start_with_context(&context, &id_c), ERROR_NONE);
    CHECK(wait_for_state(id_c, APP_INSTANCE_STATE_ACTIVE, 1000));
    CHECK_EQ(topmost_instance_id(), id_c);

    app_manager_stop(id_c);
    CHECK_EQ(topmost_instance_id(), id_b);

    app_manager_stop(id_a);
    app_manager_stop(id_b);
    app_manager_remove("test.app.top_a");
    app_manager_remove("test.app.top_b");
}

TEST_CASE("app_start honors a custom AppManifest::stack.depth") {
    ensure_fake_loader_registered();

    AppManifest manifest { "test.app.stack.custom", "Stack Custom", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    manifest.stack.depth = 4096;
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    uint32_t instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.app.stack.custom", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    CHECK_EQ(app_manager_stop(instance_id), ERROR_NONE);
    CHECK_EQ(app_manager_get_state(instance_id), APP_INSTANCE_STATE_STOPPED);

    app_manager_remove("test.app.stack.custom");
}

TEST_CASE("app_start still works when AppManifest::stack is left at its zero-value default") {
    ensure_fake_loader_registered();

    // stack.depth == 0 - app_scheduler_start() must fall back to its own default stack depth
    // rather than fail or create a zero-sized stack.
    AppManifest manifest { "test.app.stack.default", "Stack Default", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(manifest.stack.depth, 0);
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    uint32_t instance_id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.app.stack.default", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    CHECK(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));

    CHECK_EQ(app_manager_stop(instance_id), ERROR_NONE);
    CHECK_EQ(app_manager_get_state(instance_id), APP_INSTANCE_STATE_STOPPED);

    app_manager_remove("test.app.stack.default");
}

TEST_CASE("app_manager_get_topmost_app_id returns BUFFER_OVERFLOW for a too-small buffer, NOT_FOUND when nothing is active") {
    ensure_fake_loader_registered();

    char buffer[4];
    CHECK_EQ(app_manager_get_topmost_app_id(buffer, sizeof(buffer)), ERROR_NOT_FOUND);
    CHECK_EQ(app_manager_get_topmost_app_id(buffer, 0), ERROR_BUFFER_OVERFLOW);

    AppManifest manifest { "test.app.top_overflow", "Overflow", APP_CATEGORY_USER, { APP_LOCATION_PATH, nullptr } };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    uint32_t id = 0;
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.app.top_overflow", &context), ERROR_NONE);
    REQUIRE_EQ(app_start_with_context(&context, &id), ERROR_NONE);
    CHECK(wait_for_state(id, APP_INSTANCE_STATE_ACTIVE, 1000));

    // "test.app.top_overflow" doesn't fit in a 4-byte buffer.
    CHECK_EQ(app_manager_get_topmost_app_id(buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);

    app_manager_stop(id);
    app_manager_remove("test.app.top_overflow");
}
