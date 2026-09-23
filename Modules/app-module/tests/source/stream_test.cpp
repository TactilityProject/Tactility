// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/event.h>
#include <app/loader.h>
#include <app/manager.h>
#include <app/start.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <service/manager.h>

#include <tactility/delay.h>

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

// Stays Active, doing nothing, until asked to close. Just an anchor instance for
// app_stream_subscribe() to target; the tests below drive the resulting AppStream directly from
// the test thread, the same way a parent consumes a child's stream without going through its
// own fd table.
int32_t idle_app_main(int, char*[]) {
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    app_event_subscribe(&sub, &event_group);

    while (true) {
        if (task_event_group_wait_any(&event_group, nullptr, pdMS_TO_TICKS(5000)) != ERROR_NONE) {
            break; // safety net
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

AppInstanceId start_idle_app(const char* id) {
    ensure_memory_loader_registered();
    AppManifest manifest {};
    std::strncpy(manifest.id, id, sizeof(manifest.id) - 1);
    std::strncpy(manifest.name, id, sizeof(manifest.name) - 1);
    manifest.category = APP_CATEGORY_USER;
    manifest.location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(idle_app_main) };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);
    AppInstanceId instance_id = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    REQUIRE_EQ(app_start_with_context(&context, &instance_id), ERROR_NONE);
    REQUIRE(wait_for_state(instance_id, APP_INSTANCE_STATE_ACTIVE, 1000));
    return instance_id;
}

} // namespace

TEST_CASE("app_stream_read/write move bytes through a ring buffer, respecting capacity") {
    AppInstanceId producer_id = start_idle_app("test.stream.ringbuffer");

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[4];
    AppStream stream {};
    REQUIRE_EQ(app_stream_subscribe(&stream, storage, sizeof(storage), &event_group, producer_id, 5), ERROR_NONE);

    CHECK_EQ(app_stream_write(&stream, "ab", 2), 2u);
    CHECK_EQ(app_stream_write(&stream, "cdef", 4), 2u); // only 2 bytes of space left

    uint8_t out[8] = {};
    CHECK_EQ(app_stream_read(&stream, out, sizeof(out)), 4u);
    CHECK_EQ(std::memcmp(out, "abcd", 4), 0);

    app_stream_unsubscribe(&stream);
    task_event_group_destruct(&event_group);
    app_manager_stop(producer_id);
    app_manager_remove("test.stream.ringbuffer");
}

TEST_CASE("app_stream_await blocks until data/space is available, and closing forces both bits") {
    AppInstanceId producer_id = start_idle_app("test.stream.await");

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage[2];
    AppStream stream {};
    REQUIRE_EQ(app_stream_subscribe(&stream, storage, sizeof(storage), &event_group, producer_id, 5), ERROR_NONE);

    // Nothing written yet, so the readable wait times out.
    CHECK_EQ(app_stream_await(&stream, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(50)), ERROR_TIMEOUT);

    app_stream_write(&stream, "x", 1);
    CHECK_EQ(app_stream_await(&stream, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(50)), ERROR_NONE);

    app_stream_close(&stream);
    CHECK_EQ(app_stream_await(&stream, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(50)), ERROR_NONE);
    CHECK_EQ(app_stream_await(&stream, APP_FILE_WAIT_WRITABLE, pdMS_TO_TICKS(50)), ERROR_NONE);

    app_stream_unsubscribe(&stream);
    task_event_group_destruct(&event_group);
    app_manager_stop(producer_id);
    app_manager_remove("test.stream.await");
}

TEST_CASE("app_stream_subscribe over an existing subscription atomically replaces it") {
    AppInstanceId producer_id = start_idle_app("test.stream.replace");

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    uint8_t storage_a[4];
    uint8_t storage_b[4];
    AppStream stream_a {};
    REQUIRE_EQ(app_stream_subscribe(&stream_a, storage_a, sizeof(storage_a), &event_group, producer_id, 5), ERROR_NONE);

    AppStream stream_b {};
    REQUIRE_EQ(app_stream_subscribe(&stream_b, storage_b, sizeof(storage_b), &event_group, producer_id, 5), ERROR_NONE);

    // stream_a was replaced: it's now closed (readable reports ready, and reads 0 bytes / EOF),
    // even though nobody explicitly unsubscribed it.
    CHECK_EQ(app_stream_await(&stream_a, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(50)), ERROR_NONE);
    CHECK_EQ(app_stream_read(&stream_a, storage_a, sizeof(storage_a)), 0u);

    app_stream_unsubscribe(&stream_a);
    app_stream_unsubscribe(&stream_b);
    task_event_group_destruct(&event_group);
    app_manager_stop(producer_id);
    app_manager_remove("test.stream.replace");
}
