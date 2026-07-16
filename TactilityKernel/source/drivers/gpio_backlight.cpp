// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/gpio_backlight.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

#define TAG "GpioBacklight"
#define GET_CONFIG(device) (static_cast<const GpioBacklightConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<GpioBacklightInternal*>(device_get_driver_data(device)))

extern "C" {

struct GpioBacklightInternal {
    GpioDescriptor* descriptor;
    bool enabled;
};

// region GpioBacklightApi

static error_t enable(Device* device) {
    auto* internal = GET_INTERNAL(device);

    error_t error = gpio_descriptor_set_level(internal->descriptor, true);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable backlight");
        return error;
    }

    internal->enabled = true;
    return ERROR_NONE;
}

static error_t disable(Device* device) {
    auto* internal = GET_INTERNAL(device);

    error_t error = gpio_descriptor_set_level(internal->descriptor, false);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to disable backlight");
        return error;
    }

    internal->enabled = false;
    return ERROR_NONE;
}

// endregion

// region BacklightApi

static error_t gpio_backlight_set_brightness(Device* device, uint8_t brightness) {
    return brightness > 0 ? enable(device) : disable(device);
}

static error_t gpio_backlight_set_brightness_default(Device* device) {
    return GET_CONFIG(device)->enabled ? enable(device) : disable(device);
}

static error_t gpio_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    *out_brightness = GET_INTERNAL(device)->enabled ? 1 : 0;
    return ERROR_NONE;
}

static uint8_t gpio_backlight_get_min_brightness(Device*) {
    return 0;
}

static uint8_t gpio_backlight_get_max_brightness(Device*) {
    return 1;
}

// endregion

static constexpr BacklightApi GPIO_BACKLIGHT_API = {
    .set_brightness = gpio_backlight_set_brightness,
    .set_brightness_default = gpio_backlight_set_brightness_default,
    .get_brightness = gpio_backlight_get_brightness,
    .get_min_brightness = gpio_backlight_get_min_brightness,
    .get_max_brightness = gpio_backlight_get_max_brightness,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* descriptor = gpio_descriptor_acquire(config->pin.gpio_controller, config->pin.pin, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptor");
        return ERROR_RESOURCE;
    }

    if (gpio_descriptor_set_flags(descriptor, config->pin.flags | GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure backlight pin as output");
        gpio_descriptor_release(descriptor);
        return ERROR_RESOURCE;
    }

    auto* internal = new(std::nothrow) GpioBacklightInternal { .descriptor = descriptor, .enabled = false };
    if (internal == nullptr) {
        gpio_descriptor_release(descriptor);
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    gpio_backlight_set_brightness_default(device); // Allowed to fail, we don't care about the result

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    disable(device); // Allowed to fail, we don't care about the result

    auto* internal = GET_INTERNAL(device);
    gpio_descriptor_release(internal->descriptor);
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

// endregion

extern Module root_module;

Driver gpio_backlight_driver = {
    .name = "gpio_backlight",
    .compatible = (const char*[]) { "gpio-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &GPIO_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &root_module,
    .internal = nullptr
};

}
