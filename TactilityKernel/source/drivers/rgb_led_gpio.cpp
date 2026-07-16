// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/rgb_led_gpio.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

#define TAG "RgbLedGpio"
#define GET_CONFIG(device) (static_cast<const RgbLedGpioConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<RgbLedGpioInternal*>(device_get_driver_data(device)))

extern "C" {

struct RgbLedGpioInternal {
    GpioDescriptor* descriptor_red;
    GpioDescriptor* descriptor_green;
    GpioDescriptor* descriptor_blue;
    RgbLedColor color;
    bool enabled;
};

// region RgbLedApi

static error_t apply_levels(Device* device) {
    auto* internal = GET_INTERNAL(device);
    bool on = internal->enabled;

    error_t error = gpio_descriptor_set_level(internal->descriptor_red, on && internal->color.r > 0);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_set_level(internal->descriptor_green, on && internal->color.g > 0);
    }
    if (error == ERROR_NONE) {
        error = gpio_descriptor_set_level(internal->descriptor_blue, on && internal->color.b > 0);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to apply LED levels");
    }
    return error;
}

static error_t rgb_led_gpio_set_color(Device* device, RgbLedColor color) {
    GET_INTERNAL(device)->color = color;
    return apply_levels(device);
}

static error_t rgb_led_gpio_get_color(Device* device, RgbLedColor* out_color) {
    *out_color = GET_INTERNAL(device)->color;
    return ERROR_NONE;
}

static error_t rgb_led_gpio_enable(Device* device) {
    GET_INTERNAL(device)->enabled = true;
    return apply_levels(device);
}

static void rgb_led_gpio_disable(Device* device) {
    GET_INTERNAL(device)->enabled = false;
    apply_levels(device); // Allowed to fail, we don't care about the result
}

// endregion

static constexpr RgbLedApi RGB_LED_GPIO_API = {
    .set_color = rgb_led_gpio_set_color,
    .get_color = rgb_led_gpio_get_color,
    .enable = rgb_led_gpio_enable,
    .disable = rgb_led_gpio_disable,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* descriptor_red = gpio_descriptor_acquire(config->pin_red.gpio_controller, config->pin_red.pin, GPIO_OWNER_GPIO);
    if (descriptor_red == nullptr) {
        LOG_E(TAG, "Failed to acquire red GPIO descriptor");
        return ERROR_RESOURCE;
    }

    auto* descriptor_green = gpio_descriptor_acquire(config->pin_green.gpio_controller, config->pin_green.pin, GPIO_OWNER_GPIO);
    if (descriptor_green == nullptr) {
        LOG_E(TAG, "Failed to acquire green GPIO descriptor");
        gpio_descriptor_release(descriptor_red);
        return ERROR_RESOURCE;
    }

    auto* descriptor_blue = gpio_descriptor_acquire(config->pin_blue.gpio_controller, config->pin_blue.pin, GPIO_OWNER_GPIO);
    if (descriptor_blue == nullptr) {
        LOG_E(TAG, "Failed to acquire blue GPIO descriptor");
        gpio_descriptor_release(descriptor_red);
        gpio_descriptor_release(descriptor_green);
        return ERROR_RESOURCE;
    }

    bool ok = gpio_descriptor_set_flags(descriptor_red, config->pin_red.flags | GPIO_FLAG_DIRECTION_OUTPUT) == ERROR_NONE &&
        gpio_descriptor_set_flags(descriptor_green, config->pin_green.flags | GPIO_FLAG_DIRECTION_OUTPUT) == ERROR_NONE &&
        gpio_descriptor_set_flags(descriptor_blue, config->pin_blue.flags | GPIO_FLAG_DIRECTION_OUTPUT) == ERROR_NONE;
    if (!ok) {
        LOG_E(TAG, "Failed to configure LED pins as outputs");
        gpio_descriptor_release(descriptor_red);
        gpio_descriptor_release(descriptor_green);
        gpio_descriptor_release(descriptor_blue);
        return ERROR_RESOURCE;
    }

    auto* internal = new(std::nothrow) RgbLedGpioInternal {
        .descriptor_red = descriptor_red,
        .descriptor_green = descriptor_green,
        .descriptor_blue = descriptor_blue,
        .color = config->default_color,
        .enabled = false
    };
    if (internal == nullptr) {
        gpio_descriptor_release(descriptor_red);
        gpio_descriptor_release(descriptor_green);
        gpio_descriptor_release(descriptor_blue);
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    if (config->enabled) {
        rgb_led_gpio_enable(device); // Allowed to fail, we don't care about the result
    } else {
        apply_levels(device); // Allowed to fail, we don't care about the result
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    rgb_led_gpio_disable(device);

    auto* internal = GET_INTERNAL(device);
    gpio_descriptor_release(internal->descriptor_red);
    gpio_descriptor_release(internal->descriptor_green);
    gpio_descriptor_release(internal->descriptor_blue);
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

// endregion

extern Module root_module;

Driver rgb_led_gpio_driver = {
    .name = "rgb_led_gpio",
    .compatible = (const char*[]) { "rgb-led-gpio", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &RGB_LED_GPIO_API,
    .device_type = &RGB_LED_TYPE,
    .owner = &root_module,
    .internal = nullptr
};

}
