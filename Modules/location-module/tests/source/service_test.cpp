#include "doctest.h"

#include <location/location.h>
#include <location/gps_provider.h>
#include <location/location_service.h>
#include <location/provider.h>

#include <service/manager.h>

#include <tactility/delay.h>
#include <tactility/system_event.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Providers start and stop on the service's worker
std::atomic<LocationHost*> fakeHost = nullptr;
std::atomic<int> createCount = 0;
std::atomic<int> destroyCount = 0;

void* fakeCreate(LocationHost* host) {
    fakeHost = host;
    createCount++;
    return host;
}

void fakeRequestStop(void*) {}

bool fakeWaitStopped(void*, TickType_t) {
    fakeHost = nullptr;
    destroyCount++;
    return true;
}

const LocationProviderApi fakeProvider = {
    .id = "fake",
    .fallback = false,
    .create = fakeCreate,
    .request_stop = fakeRequestStop,
    .wait_stopped = fakeWaitStopped,
};

template <typename Predicate>
bool waitUntil(Predicate predicate) {
    for (int i = 0; i < 200; i++) {
        if (predicate()) {
            return true;
        }
        delay_millis(10);
    }
    return predicate();
}

Location makeLocation(double latitude, double longitude) {
    return Location {
        .latitude = latitude,
        .longitude = longitude,
        .horizontal_accuracy = 3.0f,
        .altitude = NAN,
        .ellipsoidal_altitude = NAN,
        .vertical_accuracy = NAN,
        .speed = NAN,
        .speed_accuracy = NAN,
        .course = NAN,
        .course_accuracy = NAN,
        .satellites = 0,
        .time_ms = 1000,
        .uptime_ms = 0,
        .provider = {},
    };
}

}

TEST_CASE("location service lists the descriptions of its subscribers") {
    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription first {};
    LocationSubscription second {};
    REQUIRE(location_subscribe(&first, &group, "First") == ERROR_NONE);
    REQUIRE(location_subscribe(&second, &group, "A description that is too long to fit") == ERROR_NONE);

    std::vector<std::string> descriptions;
    location_for_each_subscriber(&descriptions, [](const char* description, void* context) {
        static_cast<std::vector<std::string>*>(context)->push_back(description);
    });
    // The most recent subscription is first
    REQUIRE(descriptions.size() == 2);
    CHECK(descriptions[0] == "A description that is to");
    CHECK(descriptions[0].size() == LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH);
    CHECK(descriptions[1] == "First");

    CHECK(location_unsubscribe(&second) == ERROR_NONE);
    descriptions.clear();
    location_for_each_subscriber(&descriptions, [](const char* description, void* context) {
        static_cast<std::vector<std::string>*>(context)->push_back(description);
    });
    REQUIRE(descriptions.size() == 1);
    CHECK(descriptions[0] == "First");

    CHECK(location_unsubscribe(&first) == ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("location service reports fixes of its providers") {
    createCount = 0;
    destroyCount = 0;
    REQUIRE(location_provider_register(&fakeProvider) == ERROR_NONE);
    CHECK(location_provider_register(&fakeProvider) == ERROR_ALREADY_EXISTS);

    // Providers don't run without subscriptions
    REQUIRE(service_manager_add(&location_service_manifest, true) == ERROR_NONE);
    CHECK(createCount == 0);
    CHECK_FALSE(location_is_in_use());

    Location location;
    CHECK(location_get(&location) == ERROR_NOT_FOUND);

    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription sub {};
    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    CHECK(location_subscribe(&sub, &group, "test") == ERROR_INVALID_STATE);
    CHECK(location_is_in_use());
    REQUIRE(waitUntil([] { return fakeHost != nullptr; }));
    CHECK(createCount == 1);

    const Location reported = makeLocation(51.05, 3.72);
    location_host_report(fakeHost, &reported);

    uint32_t flags = 0;
    CHECK(task_event_group_wait(&group, sub.bit, false, &flags, 0) == ERROR_NONE);
    CHECK(flags == sub.bit);

    REQUIRE(location_get(&location) == ERROR_NONE);
    CHECK(location.latitude == doctest::Approx(51.05));
    CHECK(location.longitude == doctest::Approx(3.72));
    CHECK(location.horizontal_accuracy == doctest::Approx(3.0));
    CHECK(location.time_ms == 1000);
    CHECK(strcmp(location.provider, "fake") == 0);

    // The last known fix remains available after the last subscription is removed
    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    CHECK(location_unsubscribe(&sub) == ERROR_NOT_FOUND);
    CHECK_FALSE(location_is_in_use());
    CHECK(waitUntil([] { return destroyCount == 1; }));
    CHECK(location_get(&location) == ERROR_NONE);

    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    CHECK(waitUntil([] { return createCount == 2; }));

    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK_FALSE(location_is_in_use());
    CHECK(location_get(&location) == ERROR_NOT_FOUND);
    CHECK(waitUntil([] { return destroyCount == 2; }));

    REQUIRE(service_manager_start(location_service_manifest.id) == ERROR_NONE);
    CHECK(waitUntil([] { return createCount == 3; }));
    // Unregistering waits for the provider to stop
    CHECK(location_provider_unregister(&fakeProvider) == ERROR_NONE);
    CHECK(destroyCount == 3);
    CHECK(location_provider_unregister(&fakeProvider) == ERROR_NOT_FOUND);

    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&group);
    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK(destroyCount == 3);
    REQUIRE(service_manager_remove(location_service_manifest.id) == ERROR_NONE);
}

TEST_CASE("location provider registration is limited") {
    LocationProviderApi providers[LOCATION_PROVIDER_MAX_COUNT + 1];
    char ids[LOCATION_PROVIDER_MAX_COUNT + 1][8];
    for (size_t i = 0; i <= LOCATION_PROVIDER_MAX_COUNT; i++) {
        snprintf(ids[i], sizeof(ids[i]), "p%zu", i);
        providers[i] = { .id = ids[i], .fallback = false, .create = fakeCreate, .request_stop = fakeRequestStop, .wait_stopped = fakeWaitStopped };
    }
    for (size_t i = 0; i < LOCATION_PROVIDER_MAX_COUNT; i++) {
        REQUIRE(location_provider_register(&providers[i]) == ERROR_NONE);
    }
    CHECK(location_provider_register(&providers[LOCATION_PROVIDER_MAX_COUNT]) == ERROR_RESOURCE);
    for (size_t i = 0; i < LOCATION_PROVIDER_MAX_COUNT; i++) {
        CHECK(location_provider_unregister(&providers[i]) == ERROR_NONE);
    }
}

namespace {

std::atomic<LocationHost*> fallbackHost = nullptr;

void* fallbackCreate(LocationHost* host) {
    fallbackHost = host;
    return host;
}

void fallbackRequestStop(void*) {}

bool fallbackWaitStopped(void*, TickType_t) {
    fallbackHost = nullptr;
    return true;
}

const LocationProviderApi fakeFallbackProvider = {
    .id = "fakefallback",
    .fallback = true,
    .create = fallbackCreate,
    .request_stop = fallbackRequestStop,
    .wait_stopped = fallbackWaitStopped,
};

}

TEST_CASE("location service prefers fixes of providers that aren't a fallback") {
    REQUIRE(location_provider_register(&fakeProvider) == ERROR_NONE);
    REQUIRE(location_provider_register(&fakeFallbackProvider) == ERROR_NONE);
    REQUIRE(service_manager_add(&location_service_manifest, true) == ERROR_NONE);

    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription sub {};
    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    REQUIRE(waitUntil([] { return fakeHost != nullptr && fallbackHost != nullptr; }));

    CHECK(location_host_needs_fallback());
    const Location fallback = makeLocation(10.0, 20.0);
    location_host_report(fallbackHost, &fallback);
    CHECK(task_event_group_wait(&group, sub.bit, false, nullptr, 0) == ERROR_NONE);

    Location location;
    REQUIRE(location_get(&location) == ERROR_NONE);
    CHECK(strcmp(location.provider, "fakefallback") == 0);

    const Location primary = makeLocation(51.05, 3.72);
    location_host_report(fakeHost, &primary);
    CHECK(task_event_group_wait(&group, sub.bit, false, nullptr, 0) == ERROR_NONE);
    CHECK_FALSE(location_host_needs_fallback());
    REQUIRE(location_get(&location) == ERROR_NONE);
    CHECK(strcmp(location.provider, "fake") == 0);

    // A fallback fix doesn't replace a fresh fix, so subscribers aren't signalled
    location_host_report(fallbackHost, &fallback);
    CHECK(task_event_group_wait(&group, sub.bit, false, nullptr, 0) == ERROR_TIMEOUT);
    REQUIRE(location_get(&location) == ERROR_NONE);
    CHECK(strcmp(location.provider, "fake") == 0);

    // Without the primary provider, the fallback fix is used again
    CHECK(location_provider_unregister(&fakeProvider) == ERROR_NONE);
    CHECK(location_host_needs_fallback());
    REQUIRE(location_get(&location) == ERROR_NONE);
    CHECK(strcmp(location.provider, "fakefallback") == 0);

    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&group);
    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK(location_provider_unregister(&fakeFallbackProvider) == ERROR_NONE);
    REQUIRE(service_manager_remove(location_service_manifest.id) == ERROR_NONE);
}

namespace {

std::atomic<int> timeEventCount = 0;
std::atomic<int64_t> lastTimeEventMs = 0;

void onLocationTime(SystemEvent* event, void*) {
    LocationTimeEvent data;
    memcpy(&data, event->data, sizeof(data));
    lastTimeEventMs = data.time_ms;
    timeEventCount++;
}

}

TEST_CASE("location service announces the time of satellite fixes once per minute") {
    timeEventCount = 0;
    REQUIRE(location_provider_register(&fakeProvider) == ERROR_NONE);
    REQUIRE(location_provider_register(&fakeFallbackProvider) == ERROR_NONE);
    REQUIRE(service_manager_add(&location_service_manifest, true) == ERROR_NONE);
    REQUIRE(system_event_callback_add(KERNEL_EVENT_LOCATION_TIME, onLocationTime, nullptr) == ERROR_NONE);

    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription sub {};
    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    REQUIRE(waitUntil([] { return fakeHost != nullptr && fallbackHost != nullptr; }));

    // Fallback providers don't know the time
    Location fix = makeLocation(51.05, 3.72);
    location_host_report(fallbackHost, &fix);
    CHECK(timeEventCount == 0);

    // Fixes without a time are ignored
    fix.time_ms = 0;
    location_host_report(fakeHost, &fix);
    CHECK(timeEventCount == 0);

    fix.time_ms = 1700000000123;
    location_host_report(fakeHost, &fix);
    CHECK(timeEventCount == 1);
    CHECK(lastTimeEventMs == 1700000000123);

    // The next fix is within the same minute
    fix.time_ms += 1000;
    location_host_report(fakeHost, &fix);
    CHECK(timeEventCount == 1);

    CHECK(system_event_callback_remove(KERNEL_EVENT_LOCATION_TIME, onLocationTime) == ERROR_NONE);
    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&group);
    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK(location_provider_unregister(&fakeProvider) == ERROR_NONE);
    CHECK(location_provider_unregister(&fakeFallbackProvider) == ERROR_NONE);
    REQUIRE(service_manager_remove(location_service_manifest.id) == ERROR_NONE);
}

TEST_CASE("location_make_approximate keeps only a coarse position") {
    Location location = makeLocation(51.054321, 3.721234);
    location.altitude = 12.0f;
    location.speed = 1.5f;
    location.satellites = 7;
    location_make_approximate(&location);
    CHECK(location.latitude == doctest::Approx(51.06));
    CHECK(location.longitude == doctest::Approx(3.72));
    CHECK(location.horizontal_accuracy == doctest::Approx(LOCATION_APPROXIMATE_ACCURACY_METERS));
    CHECK(std::isnan(location.altitude));
    CHECK(std::isnan(location.speed));
    CHECK(location.satellites == 0);
    CHECK(location.time_ms == 1000);

    location.horizontal_accuracy = 25000.0f;
    location_make_approximate(&location);
    CHECK(location.horizontal_accuracy == doctest::Approx(25000.0));
}

namespace {

std::atomic<LocationHost*> stuckHost = nullptr;
std::atomic<int> stuckCreateCount = 0;
std::atomic<bool> stuck = false;

void* stuckCreate(LocationHost* host) {
    stuckHost = host;
    stuckCreateCount++;
    return host;
}

void stuckRequestStop(void*) {}

bool stuckWaitStopped(void*, TickType_t timeout) {
    if (stuck) {
        vTaskDelay(timeout);
        return false;
    }
    stuckHost = nullptr;
    return true;
}

const LocationProviderApi stuckProvider = {
    .id = "stuck",
    .fallback = false,
    .create = stuckCreate,
    .request_stop = stuckRequestStop,
    .wait_stopped = stuckWaitStopped,
};

}

TEST_CASE("location service doesn't wait for a provider that is slow to stop") {
    createCount = 0;
    destroyCount = 0;
    stuckCreateCount = 0;
    REQUIRE(location_provider_register(&fakeProvider) == ERROR_NONE);
    REQUIRE(location_provider_register(&stuckProvider) == ERROR_NONE);
    REQUIRE(service_manager_add(&location_service_manifest, true) == ERROR_NONE);

    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription sub {};
    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    REQUIRE(waitUntil([] { return fakeHost != nullptr && stuckHost != nullptr; }));
    LocationHost* stoppingHost = stuckHost;

    stuck = true;
    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    // The other provider stops while the stuck one is still stopping
    CHECK(waitUntil([] { return destroyCount == 1; }));

    // A provider that is stopping doesn't report anymore
    const Location reported = makeLocation(51.05, 3.72);
    location_host_report(stoppingHost, &reported);
    Location location;
    CHECK(location_get(&location) == ERROR_NOT_FOUND);

    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    CHECK(waitUntil([] { return createCount == 2; }));
    CHECK(stuckCreateCount == 1);

    // Once stopped, the provider starts again because it's desired again
    stuck = false;
    CHECK(waitUntil([] { return stuckCreateCount == 2; }));

    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&group);
    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK(location_provider_unregister(&fakeProvider) == ERROR_NONE);
    CHECK(location_provider_unregister(&stuckProvider) == ERROR_NONE);
    REQUIRE(service_manager_remove(location_service_manifest.id) == ERROR_NONE);
}

namespace {

/** @return the names of the location tasks that weren't deleted, separated by spaces */
std::string getLocationTasks() {
    TaskStatus_t statuses[32];
    const UBaseType_t count = uxTaskGetSystemState(statuses, 32, nullptr);
    std::string names;
    for (UBaseType_t i = 0; i < count; i++) {
        if (statuses[i].eCurrentState != eDeleted && strncmp(statuses[i].pcTaskName, "location", 8) == 0) {
            names += statuses[i].pcTaskName;
            names += ' ';
        }
    }
    return names;
}

}

TEST_CASE("location service has no background task without subscriptions") {
    REQUIRE(location_provider_register(&fakeProvider) == ERROR_NONE);
    REQUIRE(service_manager_add(&location_service_manifest, true) == ERROR_NONE);
    CHECK(waitUntil([] { return getLocationTasks().empty(); }));

    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription sub {};
    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    REQUIRE(waitUntil([] { return fakeHost != nullptr; }));
    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    REQUIRE(waitUntil([] { return fakeHost == nullptr; }));

    // The worker exits once the provider stopped
    CHECK(waitUntil([] { return getLocationTasks().empty(); }));
    INFO("location tasks: ", getLocationTasks());
    CHECK(getLocationTasks().empty());

    task_event_group_destruct(&group);
    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK(location_provider_unregister(&fakeProvider) == ERROR_NONE);
    REQUIRE(service_manager_remove(location_service_manifest.id) == ERROR_NONE);
}

TEST_CASE("the gps provider has no task without subscriptions") {
    REQUIRE(location_provider_register(&gps_location_provider) == ERROR_NONE);
    REQUIRE(service_manager_add(&location_service_manifest, true) == ERROR_NONE);
    CHECK(waitUntil([] { return getLocationTasks().empty(); }));

    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription sub {};
    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    CHECK(waitUntil([] { return getLocationTasks().find("location_gps") != std::string::npos; }));
    CHECK(location_unsubscribe(&sub) == ERROR_NONE);

    // Without a GPS device, the provider polls for one every second
    CHECK(waitUntil([] { return getLocationTasks().empty(); }));
    INFO("location tasks: ", getLocationTasks());
    CHECK(getLocationTasks().empty());

    task_event_group_destruct(&group);
    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK(location_provider_unregister(&gps_location_provider) == ERROR_NONE);
    REQUIRE(service_manager_remove(location_service_manifest.id) == ERROR_NONE);
}

TEST_CASE("selecting approximate precision rounds coarse fixes and drops precise ones") {
    REQUIRE(location_provider_register(&fakeProvider) == ERROR_NONE);
    REQUIRE(location_provider_register(&fakeFallbackProvider) == ERROR_NONE);
    REQUIRE(service_manager_add(&location_service_manifest, true) == ERROR_NONE);
    const LocationPrecision originalPrecision = location_get_precision();
    // Saving the setting fails without a data partition, but the precision changes regardless
    location_set_precision(LOCATION_PRECISION_PRECISE);

    TaskEventGroup group;
    task_event_group_construct(&group);
    LocationSubscription sub {};
    REQUIRE(location_subscribe(&sub, &group, "test") == ERROR_NONE);
    REQUIRE(waitUntil([] { return fakeHost != nullptr && fallbackHost != nullptr; }));

    Location coarse = makeLocation(51.054321, 3.721234);
    coarse.horizontal_accuracy = 25000.0f;
    location_host_report(fallbackHost, &coarse);
    const Location precise = makeLocation(10.0, 20.0);
    location_host_report(fakeHost, &precise);

    location_set_precision(LOCATION_PRECISION_APPROXIMATE);
    REQUIRE(location_get_precision() == LOCATION_PRECISION_APPROXIMATE);
    Location location;
    REQUIRE(location_get(&location) == ERROR_NONE);
    CHECK(strcmp(location.provider, "fakefallback") == 0);
    CHECK(location.latitude == doctest::Approx(51.06));
    CHECK(location.longitude == doctest::Approx(3.72));

    location_set_precision(originalPrecision);
    CHECK(location_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&group);
    REQUIRE(service_manager_stop(location_service_manifest.id) == ERROR_NONE);
    CHECK(location_provider_unregister(&fakeProvider) == ERROR_NONE);
    CHECK(location_provider_unregister(&fakeFallbackProvider) == ERROR_NONE);
    REQUIRE(service_manager_remove(location_service_manifest.id) == ERROR_NONE);
}
