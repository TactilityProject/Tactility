// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/esp32_gpio_backlight.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>

#include <cstdlib>

#define TAG "Esp32GpioBacklight"
#define GET_CONFIG(device) (static_cast<const Esp32GpioBacklightConfig*>((device)->config))

struct Esp32GpioBacklightInternal {
    GpioDescriptor* descriptor;
    uint8_t brightness;
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* descriptor = gpio_descriptor_acquire(config->pin_backlight.gpio_controller, config->pin_backlight.pin, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptor");
        return ERROR_RESOURCE;
    }

    if (gpio_descriptor_set_flags(descriptor, config->pin_backlight.flags | GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure backlight pin as output");
        gpio_descriptor_release(descriptor);
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Esp32GpioBacklightInternal*>(malloc(sizeof(Esp32GpioBacklightInternal)));
    if (internal == nullptr) {
        gpio_descriptor_release(descriptor);
        return ERROR_OUT_OF_MEMORY;
    }
    internal->descriptor = descriptor;
    internal->brightness = 0;

    device_set_driver_data(device, internal);

    backlight_set_brightness_default(device); // Allowed to fail, we don't care about the result

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    backlight_set_brightness(device, 0); // Allowed to fail, we don't care about the result

    auto* internal = static_cast<Esp32GpioBacklightInternal*>(device_get_driver_data(device));
    gpio_descriptor_release(internal->descriptor);
    free(internal);

    return ERROR_NONE;
}

// endregion

// region BacklightApi

static error_t esp32_gpio_backlight_set_brightness(Device* device, uint8_t brightness) {
    auto* internal = static_cast<Esp32GpioBacklightInternal*>(device_get_driver_data(device));

    error_t error = gpio_descriptor_set_level(internal->descriptor, brightness > 0);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to set backlight level");
        return error;
    }

    internal->brightness = brightness;
    return ERROR_NONE;
}

static error_t esp32_gpio_backlight_set_brightness_default(Device* device) {
    return esp32_gpio_backlight_set_brightness(device, GET_CONFIG(device)->default_on ? 1 : 0);
}

static error_t esp32_gpio_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    auto* internal = static_cast<Esp32GpioBacklightInternal*>(device_get_driver_data(device));
    *out_brightness = internal->brightness;
    return ERROR_NONE;
}

static uint8_t esp32_gpio_backlight_get_min_brightness(Device*) {
    return 0;
}

static uint8_t esp32_gpio_backlight_get_max_brightness(Device*) {
    return 1;
}

// endregion

static const BacklightApi esp32_gpio_backlight_api = {
    .set_brightness = esp32_gpio_backlight_set_brightness,
    .set_brightness_default = esp32_gpio_backlight_set_brightness_default,
    .get_brightness = esp32_gpio_backlight_get_brightness,
    .get_min_brightness = esp32_gpio_backlight_get_min_brightness,
    .get_max_brightness = esp32_gpio_backlight_get_max_brightness,
};

extern Module platform_esp32_module;

Driver esp32_gpio_backlight_driver = {
    .name = "esp32_gpio_backlight",
    .compatible = (const char*[]) { "espressif,esp32-gpio-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &esp32_gpio_backlight_api,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};
