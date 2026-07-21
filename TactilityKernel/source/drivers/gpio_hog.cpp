// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/gpio_hog.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>

#define TAG "GpioHog"
#define GET_CONFIG(device) (static_cast<const GpioHogConfig*>((device)->config))

namespace {
const DeviceType GPIO_HOG_TYPE { .name = "gpio_hog" };
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    gpio_flags_t flags = 0;
    bool initial_high = false;
    switch (config->mode) {
        case GPIO_HOG_MODE_OUTPUT_HIGH:
            flags = GPIO_FLAG_DIRECTION_OUTPUT;
            initial_high = true;
            break;
        case GPIO_HOG_MODE_OUTPUT_LOW:
            flags = GPIO_FLAG_DIRECTION_OUTPUT;
            break;
        case GPIO_HOG_MODE_INPUT:
            flags = GPIO_FLAG_DIRECTION_INPUT;
            break;
        default:
            return ERROR_RESOURCE;
    }

    auto* descriptor = gpio_descriptor_acquire(config->pin.gpio_controller, config->pin.pin, config->pin.flags | flags, GPIO_OWNER_HOG);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptor");
        return ERROR_RESOURCE;
    }

    if (initial_high && gpio_descriptor_set_level(descriptor, initial_high) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set initial level to %d", (int)initial_high);
        gpio_descriptor_release(descriptor);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, descriptor);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* descriptor = static_cast<GpioDescriptor*>(device_get_driver_data(device));
    gpio_descriptor_release(descriptor);
    return ERROR_NONE;
}

extern Module root_module;

Driver gpio_hog_driver = {
    .name = "gpio_hog",
    .compatible = (const char*[]) { "gpio-hog", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = &GPIO_HOG_TYPE,
    .owner = &root_module,
    .internal = nullptr
};
