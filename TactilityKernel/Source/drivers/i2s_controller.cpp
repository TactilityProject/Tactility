// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/i2s_controller.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define I2S_DRIVER_API(driver) ((struct I2sControllerApi*)driver->api)

extern "C" {

error_t i2s_controller_read(Device* device, void* data, size_t dataSize, size_t* bytesRead, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2S_DRIVER_API(driver)->read(device, data, dataSize, bytesRead, timeout);
}

error_t i2s_controller_write(Device* device, const void* data, size_t dataSize, size_t* bytesWritten, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2S_DRIVER_API(driver)->write(device, data, dataSize, bytesWritten, timeout);
}

error_t i2s_controller_set_config(Device* device, const struct I2sConfig* config) {
    const auto* driver = device_get_driver(device);
    return I2S_DRIVER_API(driver)->set_config(device, config);
}

error_t i2s_controller_get_config(Device* device, struct I2sConfig* config) {
    const auto* driver = device_get_driver(device);
    return I2S_DRIVER_API(driver)->get_config(device, config);
}

error_t i2s_controller_reset(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return I2S_DRIVER_API(driver)->reset(device);
}

extern const struct DeviceType I2S_CONTROLLER_TYPE {
    .name = "i2s-controller"
};

}
