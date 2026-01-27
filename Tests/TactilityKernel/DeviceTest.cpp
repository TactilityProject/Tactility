#include "doctest.h"

#include <cstring>
#include <vector>

#include <tactility/device.h>

TEST_CASE("device_construct and device_destruct should set and unset the correct fields") {
    Device device = { 0 };

    error_t error = device_construct(&device);
    CHECK_EQ(error, ERROR_NONE);

    CHECK_NE(device.internal.data, nullptr);
    CHECK_NE(device.internal.mutex.handle, nullptr);

    CHECK_EQ(device_destruct(&device), ERROR_NONE);

    CHECK_EQ(device.internal.data, nullptr);
    CHECK_EQ(device.internal.mutex.handle, nullptr);

    Device comparison_device = { 0 };
    comparison_device.internal.data = device.internal.data;
    comparison_device.internal.mutex.handle = device.internal.mutex.handle;

    // Check that no other data was set
    CHECK_EQ(memcmp(&device, &comparison_device, sizeof(Device)), 0);
}

TEST_CASE("device_add should add the device to the list of all devices") {
    Device device = { 0 };
    CHECK_EQ(device_construct(&device), ERROR_NONE);
    CHECK_EQ(device_add(&device), ERROR_NONE);

    // Gather all devices
    std::vector<Device*> devices;
    for_each_device(&devices, [](auto* device, auto* context) {
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
    Device parent = { 0 };

    Device child = {
        .name = nullptr,
        .config = nullptr,
        .parent = &parent
    };

    CHECK_EQ(device_construct(&parent), ERROR_NONE);
    CHECK_EQ(device_add(&parent), ERROR_NONE);

    CHECK_EQ(device_construct(&child), ERROR_NONE);
    CHECK_EQ(device_add(&child), ERROR_NONE);

    // Gather all child devices
    std::vector<Device*> children;
    for_each_device_child(&parent, &children, [](auto* child_device, auto* context) {
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
    Device device = { 0 };
    CHECK_EQ(device_construct(&device), ERROR_NONE);

    CHECK_EQ(device.internal.state.added, false);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device.internal.state.added, true);

    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_remove should remove it from the list of all devices") {
    Device device = { 0 };
    CHECK_EQ(device_construct(&device), ERROR_NONE);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device_remove(&device), ERROR_NONE);

    // Gather all devices
    std::vector<Device*> devices;
    for_each_device(&devices, [](auto* device, auto* context) {
        auto* devices_ptr = (std::vector<Device*>*)context;
        devices_ptr->push_back(device);
        return true;
    });

    CHECK_EQ(devices.size(), 0);

    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_remove should remove the device from its parent") {
    Device parent = { 0 };

    Device child = {
        .name = nullptr,
        .config = nullptr,
        .parent = &parent
    };

    CHECK_EQ(device_construct(&parent), ERROR_NONE);
    CHECK_EQ(device_add(&parent), ERROR_NONE);

    CHECK_EQ(device_construct(&child), ERROR_NONE);
    CHECK_EQ(device_add(&child), ERROR_NONE);
    CHECK_EQ(device_remove(&child), ERROR_NONE);

    // Gather all child devices
    std::vector<Device*> children;
    for_each_device_child(&parent, &children, [](auto* child_device, auto* context) {
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
    Device device = { 0 };
    CHECK_EQ(device_construct(&device), ERROR_NONE);

    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device.internal.state.added, true);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device.internal.state.added, false);

    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}

TEST_CASE("device_is_ready should return true only when it is started") {
    const char* compatible[] = { "test_compatible", nullptr };
    Driver driver = {
        .name = "test_driver",
        .compatible = compatible,
        .startDevice = nullptr,
        .stopDevice = nullptr,
        .api = nullptr,
        .deviceType = nullptr,
        .internal = { 0 }
    };

    Device device = { 0 };

    CHECK_EQ(driver_construct(&driver), ERROR_NONE);
    CHECK_EQ(device_construct(&device), ERROR_NONE);

    CHECK_EQ(device.internal.state.started, false);
    device_set_driver(&device, &driver);
    CHECK_EQ(device.internal.state.started, false);
    CHECK_EQ(device_add(&device), ERROR_NONE);
    CHECK_EQ(device.internal.state.started, false);
    CHECK_EQ(device_start(&device), ERROR_NONE);
    CHECK_EQ(device.internal.state.started, true);
    CHECK_EQ(device_stop(&device), ERROR_NONE);
    CHECK_EQ(device.internal.state.started, false);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device.internal.state.started, false);

    CHECK_EQ(driver_destruct(&driver), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
}
