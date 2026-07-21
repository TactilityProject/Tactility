#include "doctest.h"

#include <vector>

#include <tactility/device_listener.h>

// Declared in device_listener.cpp's private header; forward-declared here rather than
// including the private header, matching the pattern used for other internal-only hooks.
extern "C" void device_listener_notify(Device* dev, DeviceEvent event);

static std::vector<std::pair<void*, DeviceEvent>> calls_a;
static std::vector<std::pair<void*, DeviceEvent>> calls_b;

static void listener_a(Device* dev, DeviceEvent event, void* context) {
    calls_a.push_back({ context, event });
}

static void listener_b(Device* dev, DeviceEvent event, void* context) {
    calls_b.push_back({ context, event });
}

static void reset_calls() {
    calls_a.clear();
    calls_b.clear();
}

TEST_CASE("device_listener_notify invokes every registered listener with its own context") {
    reset_calls();
    int context_a = 1;
    int context_b = 2;

    device_listener_add(listener_a, &context_a);
    device_listener_add(listener_b, &context_b);

    auto* fake_device = reinterpret_cast<Device*>(0x1000);
    device_listener_notify(fake_device, DEVICE_EVENT_STARTED);

    CHECK_EQ(calls_a.size(), 1);
    CHECK_EQ(calls_a[0].first, &context_a);
    CHECK_EQ(calls_a[0].second, DEVICE_EVENT_STARTED);

    CHECK_EQ(calls_b.size(), 1);
    CHECK_EQ(calls_b[0].first, &context_b);
    CHECK_EQ(calls_b[0].second, DEVICE_EVENT_STARTED);

    device_listener_remove(listener_a);
    device_listener_remove(listener_b);
}

TEST_CASE("device_listener_remove stops further notifications for that callback only") {
    reset_calls();
    int context_a = 1;
    int context_b = 2;

    device_listener_add(listener_a, &context_a);
    device_listener_add(listener_b, &context_b);

    device_listener_remove(listener_a);

    auto* fake_device = reinterpret_cast<Device*>(0x1000);
    device_listener_notify(fake_device, DEVICE_EVENT_STOPPED);

    CHECK_EQ(calls_a.size(), 0);
    CHECK_EQ(calls_b.size(), 1);

    device_listener_remove(listener_b);
}

TEST_CASE("device_listener_remove on an unregistered callback is a no-op") {
    reset_calls();
    int context_b = 2;
    device_listener_add(listener_b, &context_b);

    // listener_a was never added, so removing it must not disturb listener_b.
    device_listener_remove(listener_a);

    auto* fake_device = reinterpret_cast<Device*>(0x1000);
    device_listener_notify(fake_device, DEVICE_EVENT_STARTED);

    CHECK_EQ(calls_b.size(), 1);

    device_listener_remove(listener_b);
}

static bool reentrant_add_triggered = false;

static void reentrant_listener(Device* dev, DeviceEvent event, void* context) {
    calls_a.push_back({ context, event });
    if (!reentrant_add_triggered) {
        reentrant_add_triggered = true;
        // Adding a listener from within a notification must not deadlock: notify() takes a
        // snapshot of the listener list under the lock, then invokes callbacks after unlocking.
        device_listener_add(listener_b, context);
    }
}

TEST_CASE("device_listener_notify is safe when a listener adds another listener during notification") {
    reset_calls();
    reentrant_add_triggered = false;
    int context_a = 1;

    device_listener_add(reentrant_listener, &context_a);

    auto* fake_device = reinterpret_cast<Device*>(0x1000);
    device_listener_notify(fake_device, DEVICE_EVENT_STARTED);

    // The listener added during this round of notification was not part of the snapshot,
    // so it should not have been invoked yet.
    CHECK_EQ(calls_a.size(), 1);
    CHECK_EQ(calls_b.size(), 0);

    // A second round picks up the newly-added listener.
    device_listener_notify(fake_device, DEVICE_EVENT_STOPPED);
    CHECK_EQ(calls_a.size(), 2);
    CHECK_EQ(calls_b.size(), 1);

    device_listener_remove(reentrant_listener);
    device_listener_remove(listener_b);
}
