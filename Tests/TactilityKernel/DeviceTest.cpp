#include "doctest.h"

#include <cstring>
#include <vector>

#include <Tactility/Device.h>

TEST_CASE("device_construct and device_destruct should set and unset the correct fields") {
    Device device = { 0 };

    int error = device_construct(&device);
    CHECK_EQ(error, 0);

    CHECK_NE(device.internal.data, nullptr);
    CHECK_NE(device.internal.mutex.handle, nullptr);

    error = device_destruct(&device);
    CHECK_EQ(error, 0);

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
    CHECK_EQ(device_construct(&device), 0);
    CHECK_EQ(device_add(&device), 0);

    // Gather all devices
    std::vector<Device*> devices;
    for_each_device(&devices, [](auto* device, auto* context) {
        auto* devices_ptr = (std::vector<Device*>*)context;
        devices_ptr->push_back(device);
        return true;
    });

    CHECK_EQ(devices.size(), 1);
    CHECK_EQ(devices[0], &device);

    CHECK_EQ(device_remove(&device), 0);
    CHECK_EQ(device_destruct(&device), 0);
}

TEST_CASE("device_add should add the device to its parent") {
    Device parent = { 0 };

    Device child = {
        .name = nullptr,
        .config = nullptr,
        .parent = &parent
    };

    CHECK_EQ(device_construct(&parent), 0);
    CHECK_EQ(device_add(&parent), 0);

    CHECK_EQ(device_construct(&child), 0);
    CHECK_EQ(device_add(&child), 0);

    // Gather all child devices
    std::vector<Device*> children;
    for_each_device_child(&parent, &children, [](auto* child_device, auto* context) {
        auto* children_ptr = (std::vector<Device*>*)context;
        children_ptr->push_back(child_device);
        return true;
    });

    CHECK_EQ(children.size(), 1);
    CHECK_EQ(children[0], &child);

    CHECK_EQ(device_remove(&child), 0);
    CHECK_EQ(device_destruct(&child), 0);

    CHECK_EQ(device_remove(&parent), 0);
    CHECK_EQ(device_destruct(&parent), 0);
}

TEST_CASE("device_add should set the state to 'added'") {
    Device device = { 0 };
    CHECK_EQ(device_construct(&device), 0);

    CHECK_EQ(device.internal.state.added, false);
    CHECK_EQ(device_add(&device), 0);
    CHECK_EQ(device.internal.state.added, true);

    CHECK_EQ(device_remove(&device), 0);
    CHECK_EQ(device_destruct(&device), 0);
}

TEST_CASE("device_remove should remove it from the list of all devices") {
    Device device = { 0 };
    CHECK_EQ(device_construct(&device), 0);
    CHECK_EQ(device_add(&device), 0);
    CHECK_EQ(device_remove(&device), 0);

    // Gather all devices
    std::vector<Device*> devices;
    for_each_device(&devices, [](auto* device, auto* context) {
        auto* devices_ptr = (std::vector<Device*>*)context;
        devices_ptr->push_back(device);
        return true;
    });

    CHECK_EQ(devices.size(), 0);

    CHECK_EQ(device_destruct(&device), 0);
}

TEST_CASE("device_remove should remove the device from its parent") {
    Device parent = { 0 };

    Device child = {
        .name = nullptr,
        .config = nullptr,
        .parent = &parent
    };

    CHECK_EQ(device_construct(&parent), 0);
    CHECK_EQ(device_add(&parent), 0);

    CHECK_EQ(device_construct(&child), 0);
    CHECK_EQ(device_add(&child), 0);
    CHECK_EQ(device_remove(&child), 0);

    // Gather all child devices
    std::vector<Device*> children;
    for_each_device_child(&parent, &children, [](auto* child_device, auto* context) {
        auto* children_ptr = (std::vector<Device*>*)context;
        children_ptr->push_back(child_device);
        return true;
    });

    CHECK_EQ(children.size(), 0);

    CHECK_EQ(device_destruct(&child), 0);

    CHECK_EQ(device_remove(&parent), 0);
    CHECK_EQ(device_destruct(&parent), 0);
}

TEST_CASE("device_remove should clear the state 'added'") {
    Device device = { 0 };
    CHECK_EQ(device_construct(&device), 0);

    CHECK_EQ(device_add(&device), 0);
    CHECK_EQ(device.internal.state.added, true);
    CHECK_EQ(device_remove(&device), 0);
    CHECK_EQ(device.internal.state.added, false);

    CHECK_EQ(device_destruct(&device), 0);
}

TEST_CASE("device_is_ready should return true only when it is started") {
    Driver driver = {
        .name = "test_driver",
        .compatible = (const char*[]) { "test_compatible", nullptr },
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .internal = { 0 }
    };

    Device device = { 0 };

    CHECK_EQ(driver_construct(&driver), 0);
    CHECK_EQ(device_construct(&device), 0);

    CHECK_EQ(device.internal.state.started, false);
    device_set_driver(&device, &driver);
    CHECK_EQ(device.internal.state.started, false);
    CHECK_EQ(device_add(&device), 0);
    CHECK_EQ(device.internal.state.started, false);
    CHECK_EQ(device_start(&device), 0);
    CHECK_EQ(device.internal.state.started, true);
    CHECK_EQ(device_stop(&device), 0);
    CHECK_EQ(device.internal.state.started, false);
    CHECK_EQ(device_remove(&device), 0);
    CHECK_EQ(device.internal.state.started, false);

    CHECK_EQ(driver_destruct(&driver), 0);
    CHECK_EQ(device_destruct(&device), 0);
}
