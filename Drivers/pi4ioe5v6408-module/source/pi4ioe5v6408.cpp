// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/module.h>

#include <tactility/log.h>
#include <pi4ioe5v6408_module.h>

#define TAG "PI4IOE5V6408"

#define GET_DATA(device) ((Pi4DevicePrivate*)device_get_driver_data(device))

static error_t start(Device* device) {
	auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent device is not I2C");
        return ERROR_RESOURCE;
    }
    LOG_I(TAG, "Started PI4IOE5V6408 device %s", device->name);
	return ERROR_NONE;
}

static error_t stop(Device* device) {
	return ERROR_NONE;
}

extern "C" {

Driver pi4ioe5v6408_driver = {
    .name = "pi4ioe5v6408",
    .compatible = (const char*[]) { "diodes,pi4ioe5v6408", nullptr},
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &pi4ioe5v6408_module,
    .internal = nullptr
};

}