#include "doctest.h"

#include <string.h>
#include <vector>

#include <Tactility/Device.h>

TEST_CASE("device_construct and device_destruct should set and unset the correct fields") {
    Device device = { 0 };

    device_construct(&device);

    CHECK_NE(device.internal.data, nullptr);
    CHECK_NE(device.internal.mutex.handle, nullptr);

    device_destruct(&device);

    CHECK_EQ(device.internal.data, nullptr);
    CHECK_EQ(device.internal.mutex.handle, nullptr);

    Device comparison_device = { 0 };
    comparison_device.internal.data = device.internal.data;
    comparison_device.internal.mutex.handle = device.internal.mutex.handle;

    // Check that no other data was set
    CHECK_EQ(memcmp(&device, &comparison_device, sizeof(struct Device)), 0);
}

TEST_CASE("device_add should make the device discoverable") {
    Device device = { 0 };
    device_construct(&device);
    device_add(&device);

    // Gather all devices
    std::vector<Device*> devices;
    for_each_device(&devices, [](auto* device, auto* context) {
        auto* devices_ptr = (std::vector<Device*>*)context;
        devices_ptr->push_back(device);
        return true;
    });

    CHECK_EQ(devices.size(), 1);
    CHECK_EQ(devices[0], &device);

    device_remove(&device);
    device_destruct(&device);
}

TEST_CASE("device_add should add the device to its parent") {
    Device parent = { 0 };

    Device child = {
        .name = nullptr,
        .config = nullptr,
        .parent = &parent
    };

    device_construct(&parent);
    device_add(&parent);

    device_construct(&child);
    device_add(&child);

    // Gather all child devices
    std::vector<Device*> children;
    for_each_device_child(&parent, &children, [](auto* child_device, auto* context) {
        auto* children_ptr = (std::vector<Device*>*)context;
        children_ptr->push_back(child_device);
        return true;
    });

    CHECK_EQ(children.size(), 1);
    CHECK_EQ(children[0], &child);

    device_remove(&child);
    device_destruct(&child);

    device_remove(&parent);
    device_destruct(&parent);
}

TEST_CASE("device_add should set the state to 'added'") {
    Device device = { 0 };
    device_construct(&device);

    CHECK_EQ(device.internal.state.added, false);
    device_add(&device);
    CHECK_EQ(device.internal.state.added, true);

    device_remove(&device);
    device_destruct(&device);
}
