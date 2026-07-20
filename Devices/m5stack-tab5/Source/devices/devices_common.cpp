#include "devices_common.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>

constexpr auto* TAG = "Tab5";

// Mirrors device_construct_add_start(), but with a device_set_parent() call inserted between
// construct and add - device_set_parent() asserts on device->internal (only valid after
// construct), and device_construct_add_start() doesn't expose a hook to call it before add().
bool construct_add_start(Device* device, Device* parent, const char* compatible) {
    error_t error = device_construct(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "display_detect: failed to construct %s: %s", device->name, error_to_string(error));
        return false;
    }

    device_set_parent(device, parent);

    Driver* driver = driver_find_compatible(compatible);
    if (driver == nullptr) {
        LOG_E(TAG, "display_detect: no driver registered for %s", compatible);
        device_destruct(device);
        return false;
    }
    device_set_driver(device, driver);

    error = device_add(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "display_detect: failed to add %s: %s", device->name, error_to_string(error));
        device_destruct(device);
        return false;
    }

    error = device_start(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "display_detect: failed to start %s: %s", device->name, error_to_string(error));
        device_remove(device);
        device_destruct(device);
        return false;
    }

    return true;
}
