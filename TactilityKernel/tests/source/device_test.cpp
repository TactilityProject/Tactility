#include "doctest.h"

#include <cstring>
#include <vector>

#include <tactility/concurrent/thread.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/device_listener.h>

TEST_CASE("device_construct and device_destruct should set and unset the constructed state") {
    Device device = { 0 };

    error_t error = device_construct(&device);
    CHECK_EQ(error, ERROR_NONE);

    CHECK_EQ(device_is_constructed(&device), true);

    CHECK_EQ(device_destruct(&device), ERROR_NONE);

    CHECK_EQ(device_is_constructed(&device), false);
}

TEST_CASE("device_construct should be reusable after device_destruct on the same Device") {
    Device device = { 0 };

    CHECK_EQ(device_construct(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);

    // Reconstruct on the same static Device: fresh allocation, behaves like a new device.
    CHECK_EQ(device_construct(&device), ERROR_NONE);
    CHECK_EQ(device_is_constructed(&device), true);
    CHECK_EQ(device_is_added(&device), false);

    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_add should add the device to the list of all devices") {
    Device device = {
        .name = "device",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr
    };
    CHECK_EQ(device_construct(&device), ERROR_NONE);
    CHECK_EQ(device_add(&device), ERROR_NONE);

    // Gather all devices
    std::vector<Device*> devices;
    device_for_each(&devices, [](auto* device, auto* context) {
        auto* devices_ptr = static_cast<std::vector<Device*>*>(context);
        devices_ptr->push_back(device);
        return true;
    });

    CHECK_EQ(devices.size(), 1);
    CHECK_EQ(devices[0], &device);

    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_add should add the device to its parent") {
    Device parent = {
        .name = "parent",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr
    };

    Device child = {
        .name = "child",
        .config = nullptr,
        .parent = &parent,
        .internal = nullptr
    };

    CHECK_EQ(device_construct(&parent), ERROR_NONE);
    CHECK_EQ(device_add(&parent), ERROR_NONE);

    CHECK_EQ(device_construct(&child), ERROR_NONE);
    CHECK_EQ(device_add(&child), ERROR_NONE);

    // Gather all child devices
    std::vector<Device*> children;
    device_for_each_child(&parent, &children, [](auto* child_device, auto* context) {
        auto* children_ptr = (std::vector<Device*>*)context;
        children_ptr->push_back(child_device);
        return true;
    });

    CHECK_EQ(children.size(), 1);
    CHECK_EQ(children[0], &child);

    CHECK_EQ(device_remove(&child), ERROR_NONE);
    CHECK_EQ(device_destruct(&child), ERROR_NONE);

    CHECK_EQ(device_remove(&parent), ERROR_NONE);
    CHECK_EQ(device_destruct(&parent), ERROR_NONE);
}

TEST_CASE("device_add should set the state to 'added'") {
    Device device = {
        .name = "device",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr
    };

    CHECK_EQ(device_construct(&device), ERROR_NONE);

    CHECK_EQ(device_is_added(&device), false);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device_is_added(&device), true);

    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_remove should remove it from the list of all devices") {
    Device device = {
        .name = "device",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr
    };

    CHECK_EQ(device_construct(&device), ERROR_NONE);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device_remove(&device), ERROR_NONE);

    // Gather all devices
    std::vector<Device*> devices;
    device_for_each(&devices, [](auto* device, auto* context) {
        auto* devices_ptr = (std::vector<Device*>*)context;
        devices_ptr->push_back(device);
        return true;
    });

    CHECK_EQ(devices.size(), 0);

    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_remove should remove the device from its parent") {
    Device parent = {
        .name = "parent",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr
    };

    Device child = {
        .name = "child",
        .config = nullptr,
        .parent = &parent,
        .internal = nullptr
    };

    CHECK_EQ(device_construct(&parent), ERROR_NONE);
    CHECK_EQ(device_add(&parent), ERROR_NONE);

    CHECK_EQ(device_construct(&child), ERROR_NONE);
    CHECK_EQ(device_add(&child), ERROR_NONE);
    CHECK_EQ(device_remove(&child), ERROR_NONE);

    // Gather all child devices
    std::vector<Device*> children;
    device_for_each_child(&parent, &children, [](auto* child_device, auto* context) {
        auto* children_ptr = (std::vector<Device*>*)context;
        children_ptr->push_back(child_device);
        return true;
    });

    CHECK_EQ(children.size(), 0);

    CHECK_EQ(device_destruct(&child), ERROR_NONE);

    CHECK_EQ(device_remove(&parent), ERROR_NONE);
    CHECK_EQ(device_destruct(&parent), ERROR_NONE);
}

TEST_CASE("device_remove should clear the state 'added'") {
    Device device = {
        .name = "device",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr
    };

    CHECK_EQ(device_construct(&device), ERROR_NONE);

    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device_is_added(&device), true);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_is_added(&device), false);

    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_is_ready should return true only when it is started") {
    const char* compatible[] = { "test_compatible", nullptr };
    Driver driver = {
        .name = "test_driver",
        .compatible = compatible,
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .internal = nullptr
    };

    Device device = { 0 };

    CHECK_EQ(driver_construct_add(&driver), ERROR_NONE);
    CHECK_EQ(device_construct(&device), ERROR_NONE);

    CHECK_EQ(device_is_ready(&device), false);
    device_set_driver(&device, &driver);
    CHECK_EQ(device_is_ready(&device), false);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device_is_ready(&device), false);
    CHECK_EQ(device_start(&device), ERROR_NONE);
    CHECK_EQ(device_is_ready(&device), true);
    CHECK_EQ(device_stop(&device), ERROR_NONE);
    CHECK_EQ(device_is_ready(&device), false);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_is_ready(&device), false);

    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&driver), ERROR_NONE);
}

struct StoppingObservation {
    Device* device = nullptr;
    bool listenerCalled = false;
    bool stoppingInListener = false;
    bool stoppingInDriver = false;
};

static StoppingObservation stopping_observation;

static void stopping_listener(Device* device, DeviceEvent event, void*) {
    if (device == stopping_observation.device && event == DEVICE_EVENT_STOPPING) {
        stopping_observation.listenerCalled = true;
        stopping_observation.stoppingInListener = device_is_stopping(device);
    }
}

static error_t stopping_driver_stop(Device* device) {
    stopping_observation.stoppingInDriver = device_is_stopping(device);
    return ERROR_NONE;
}

TEST_CASE("device_is_stopping should be true for the stopping listeners and the driver's stop") {
    const char* compatible[] = { "test_compatible", nullptr };
    Driver driver = {
        .name = "test_driver",
        .compatible = compatible,
        .start_device = nullptr,
        .stop_device = stopping_driver_stop,
        .api = nullptr,
        .device_type = nullptr,
        .internal = nullptr
    };

    Device device = { 0 };
    stopping_observation = {};
    stopping_observation.device = &device;

    CHECK_EQ(driver_construct_add(&driver), ERROR_NONE);
    CHECK_EQ(device_construct(&device), ERROR_NONE);
    device_set_driver(&device, &driver);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    device_listener_add(stopping_listener, nullptr);

    CHECK_EQ(device_is_stopping(&device), false);
    CHECK_EQ(device_start(&device), ERROR_NONE);
    CHECK_EQ(device_is_stopping(&device), false);
    CHECK_EQ(device_stop(&device), ERROR_NONE);
    CHECK_EQ(device_is_stopping(&device), false);
    CHECK(stopping_observation.listenerCalled);
    CHECK(stopping_observation.stoppingInListener);
    CHECK(stopping_observation.stoppingInDriver);

    device_listener_remove(stopping_listener, nullptr);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&driver), ERROR_NONE);
}

TEST_CASE("device_stop should not start stopping while another task holds the device lock") {
    const char* compatible[] = { "test_compatible", nullptr };
    Driver driver = {
        .name = "test_driver",
        .compatible = compatible,
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .internal = nullptr
    };

    Device device = { 0 };
    stopping_observation = {};
    stopping_observation.device = &device;

    CHECK_EQ(driver_construct_add(&driver), ERROR_NONE);
    CHECK_EQ(device_construct(&device), ERROR_NONE);
    device_set_driver(&device, &driver);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device_start(&device), ERROR_NONE);
    device_listener_add(stopping_listener, nullptr);

    // Like a driver call that checks the state and registers a subscriber under the device lock
    device_lock(&device);
    auto* thread = thread_alloc_full(
        "device stop",
        4096,
        [](void* context) {
            return static_cast<int32_t>(device_stop(static_cast<Device*>(context)));
        },
        &device,
        -1
    );
    REQUIRE(thread != nullptr);
    CHECK_EQ(thread_start(thread), ERROR_NONE);
    delay_millis(50);
    CHECK_EQ(device_is_stopping(&device), false);
    CHECK_FALSE(stopping_observation.listenerCalled);
    device_unlock(&device);

    CHECK_EQ(thread_join(thread, pdMS_TO_TICKS(2000), 1), ERROR_NONE);
    thread_free(thread);
    CHECK(stopping_observation.listenerCalled);
    CHECK_EQ(device_is_ready(&device), false);

    device_listener_remove(stopping_listener, nullptr);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&driver), ERROR_NONE);
}
