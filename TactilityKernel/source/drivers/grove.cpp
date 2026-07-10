// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/grove.h>
#include <tactility/device.h>

#define GROVE_DRIVER_API(driver) ((struct GroveApi*)driver->api)

extern "C" {

error_t grove_set_mode(Device* device, GroveMode mode) {
    const auto* driver = device_get_driver(device);
    return GROVE_DRIVER_API(driver)->set_mode(device, mode);
}

error_t grove_get_mode(Device* device, GroveMode* mode) {
    const auto* driver = device_get_driver(device);
    return GROVE_DRIVER_API(driver)->get_mode(device, mode);
}

const DeviceType GROVE_TYPE {
    .name = "grove"
};

}
