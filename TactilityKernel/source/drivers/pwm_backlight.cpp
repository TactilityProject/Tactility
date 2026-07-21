// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/pwm_backlight.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/pwm.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

#define TAG "PwmBacklight"
#define GET_CONFIG(device) (static_cast<const PwmBacklightConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<PwmBacklightInternal*>(device_get_driver_data(device)))

extern "C" {

struct PwmBacklightInternal {
    uint8_t brightness;
};

// region BacklightApi

static error_t apply_brightness(Device* device, uint8_t brightness) {
    const auto* config = GET_CONFIG(device);

    if (brightness <= config->brightness_range.min) {
        error_t error = pwm_disable(config->pwm);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to disable PWM");
            return error;
        }
        return ERROR_NONE;
    }

    if (brightness > config->brightness_range.max) {
        brightness = config->brightness_range.max;
    }

    uint32_t period_ns;
    error_t error = pwm_get_period(config->pwm, &period_ns);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to get PWM period");
        return error;
    }

    uint8_t level = brightness - config->brightness_range.min;
    uint8_t range = config->brightness_range.max - config->brightness_range.min;
    uint32_t duty_ns = (uint32_t)(((uint64_t)level * period_ns) / range);

    error = pwm_set_duty(config->pwm, duty_ns);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to set PWM duty");
        return error;
    }

    return pwm_enable(config->pwm);
}

static error_t pwm_backlight_set_brightness(Device* device, uint8_t brightness) {
    error_t error = apply_brightness(device, brightness);
    if (error != ERROR_NONE) {
        return error;
    }
    GET_INTERNAL(device)->brightness = brightness;
    return ERROR_NONE;
}

static error_t pwm_backlight_set_brightness_default(Device* device) {
    return pwm_backlight_set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t pwm_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    *out_brightness = GET_INTERNAL(device)->brightness;
    return ERROR_NONE;
}

static uint8_t pwm_backlight_get_min_brightness(Device* device) {
    return GET_CONFIG(device)->brightness_range.min;
}

static uint8_t pwm_backlight_get_max_brightness(Device* device) {
    return GET_CONFIG(device)->brightness_range.max;
}

// endregion

static constexpr BacklightApi PWM_BACKLIGHT_API = {
    .set_brightness = pwm_backlight_set_brightness,
    .set_brightness_default = pwm_backlight_set_brightness_default,
    .get_brightness = pwm_backlight_get_brightness,
    .get_min_brightness = pwm_backlight_get_min_brightness,
    .get_max_brightness = pwm_backlight_get_max_brightness,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (config->brightness_range.max <= config->brightness_range.min) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = new(std::nothrow) PwmBacklightInternal { .brightness = GET_CONFIG(device)->brightness_range.min };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    pwm_backlight_set_brightness_default(device); // Allowed to fail, we don't care about the result

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    pwm_backlight_set_brightness(device, GET_CONFIG(device)->brightness_range.min); // Allowed to fail, we don't care about the result

    auto* internal = GET_INTERNAL(device);
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

// endregion

extern Module root_module;

Driver pwm_backlight_driver = {
    .name = "pwm_backlight",
    .compatible = (const char*[]) { "pwm-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &PWM_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &root_module,
    .internal = nullptr
};

}
