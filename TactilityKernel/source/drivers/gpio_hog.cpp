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
const struct DeviceType GPIO_HOG_TYPE { .name = "gpio_hog" };
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* descriptor = gpio_descriptor_acquire(config->pin.gpio_controller, config->pin.pin, GPIO_OWNER_HOG);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptor");
        return ERROR_RESOURCE;
    }

    if (gpio_descriptor_set_flags(descriptor, GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE ||
        gpio_descriptor_set_level(descriptor, config->output_high) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure hogged pin");
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
    .compatible = (const char*[]) { "tactility,gpio-hog", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = &GPIO_HOG_TYPE,
    .owner = &root_module,
    .internal = nullptr
};
