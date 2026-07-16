// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/rgb_led_pwm.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/pwm.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

#define TAG "RgbLedPwm"
#define GET_CONFIG(device) (static_cast<const RgbLedPwmConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<RgbLedPwmInternal*>(device_get_driver_data(device)))

extern "C" {

struct RgbLedPwmInternal {
    RgbLedColor color;
    bool enabled;
};

// region RgbLedApi

static error_t apply_channel(Device* pwm_device, uint8_t component) {
    uint32_t period_ns;
    error_t error = pwm_get_period(pwm_device, &period_ns);
    if (error != ERROR_NONE) {
        return error;
    }

    uint32_t duty_ns = (uint32_t)(((uint64_t)component * period_ns) / 255);
    return pwm_set_duty(pwm_device, duty_ns);
}

static error_t apply_color(Device* device) {
    const auto* config = GET_CONFIG(device);
    const auto* internal = GET_INTERNAL(device);

    error_t error = apply_channel(config->pwm_red, internal->color.r);
    if (error == ERROR_NONE) {
        error = apply_channel(config->pwm_green, internal->color.g);
    }
    if (error == ERROR_NONE) {
        error = apply_channel(config->pwm_blue, internal->color.b);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to apply LED color");
    }
    return error;
}

static error_t rgb_led_pwm_set_color(Device* device, RgbLedColor color) {
    GET_INTERNAL(device)->color = color;
    return apply_color(device);
}

static error_t rgb_led_pwm_get_color(Device* device, RgbLedColor* out_color) {
    *out_color = GET_INTERNAL(device)->color;
    return ERROR_NONE;
}

static error_t rgb_led_pwm_enable(Device* device) {
    const auto* config = GET_CONFIG(device);
    GET_INTERNAL(device)->enabled = true;

    error_t error = pwm_enable(config->pwm_red);
    if (error == ERROR_NONE) {
        error = pwm_enable(config->pwm_green);
    }
    if (error == ERROR_NONE) {
        error = pwm_enable(config->pwm_blue);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable LED");
    }
    return error;
}

static void rgb_led_pwm_disable(Device* device) {
    const auto* config = GET_CONFIG(device);
    GET_INTERNAL(device)->enabled = false;

    pwm_disable(config->pwm_red);
    pwm_disable(config->pwm_green);
    pwm_disable(config->pwm_blue);
}

// endregion

static constexpr RgbLedApi RGB_LED_PWM_API = {
    .set_color = rgb_led_pwm_set_color,
    .get_color = rgb_led_pwm_get_color,
    .enable = rgb_led_pwm_enable,
    .disable = rgb_led_pwm_disable,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = new(std::nothrow) RgbLedPwmInternal {
        .color = config->default_color,
        .enabled = false
    };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    error_t error = apply_color(device);
    if (error != ERROR_NONE) {
        device_set_driver_data(device, nullptr);
        delete internal;
        return error;
    }

    if (config->enabled) {
        rgb_led_pwm_enable(device); // Allowed to fail, we don't care about the result
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    rgb_led_pwm_disable(device);

    auto* internal = GET_INTERNAL(device);
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

// endregion

extern Module root_module;

Driver rgb_led_pwm_driver = {
    .name = "rgb_led_pwm",
    .compatible = (const char*[]) { "rgb-led-pwm", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &RGB_LED_PWM_API,
    .device_type = &RGB_LED_TYPE,
    .owner = &root_module,
    .internal = nullptr
};

}
