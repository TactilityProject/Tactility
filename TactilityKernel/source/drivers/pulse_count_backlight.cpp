// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/pulse_count_backlight.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/freertos/task.h>
#include <tactility/log.h>

#include <new>

#define TAG "PulseCountBacklight"
#define GET_CONFIG(device) (static_cast<const PulseCountBacklightConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<PulseCountBacklightInternal*>(device_get_driver_data(device)))

// AW9364 timing: a low pulse of 0.5 to 500 us lowers the level, a low of more than 2.5 ms turns the IC off.
// After turning on, the IC needs a high of at least 30 us before the first pulse.
constexpr uint32_t PULSE_MICROS = 1;
constexpr uint32_t OFF_MICROS = 3000;
constexpr uint32_t ON_MICROS = 30;
/** The level of the IC is not known, e.g. after a failed GPIO write */
constexpr uint16_t LEVEL_UNKNOWN = 0x100;

extern "C" {

struct PulseCountBacklightInternal {
    GpioDescriptor* descriptor;
    /** Serializes brightness changes so the level matches the state of the IC */
    Mutex mutex;
    /** 0 when off, LEVEL_UNKNOWN when not known */
    uint16_t level;
};

// region BacklightApi

static error_t turn_off(PulseCountBacklightInternal* internal) {
    internal->level = LEVEL_UNKNOWN;
    const error_t error = gpio_descriptor_set_level(internal->descriptor, false);
    if (error != ERROR_NONE) {
        return error;
    }
    delay_micros(OFF_MICROS);
    internal->level = 0;
    return ERROR_NONE;
}

/** Each pulse lowers the level by one, and the lowest level wraps around to the highest */
static error_t send_pulses(PulseCountBacklightInternal* internal, uint32_t count) {
    error_t error = ERROR_NONE;
    // A task switch during a low pulse could make it long enough to turn the IC off
    vTaskSuspendAll();
    for (uint32_t i = 0; i < count && error == ERROR_NONE; i++) {
        error = gpio_descriptor_set_level(internal->descriptor, false);
        delay_micros(PULSE_MICROS);
        if (error == ERROR_NONE) {
            error = gpio_descriptor_set_level(internal->descriptor, true);
        }
        delay_micros(PULSE_MICROS);
    }
    xTaskResumeAll();
    return error;
}

static error_t set_brightness_locked(Device* device, uint8_t brightness) {
    const auto* config = GET_CONFIG(device);
    auto* internal = GET_INTERNAL(device);
    // Values of a wider range, e.g. from settings that were saved for another backlight driver
    if (brightness > config->levels) {
        brightness = config->levels;
    }

    if (internal->level == LEVEL_UNKNOWN || brightness == 0) {
        const error_t error = turn_off(internal);
        if (error != ERROR_NONE || brightness == 0) {
            return error;
        }
    }

    if (internal->level == 0) {
        const error_t error = gpio_descriptor_set_level(internal->descriptor, true);
        if (error != ERROR_NONE) {
            internal->level = LEVEL_UNKNOWN;
            return error;
        }
        delay_micros(ON_MICROS);
        internal->level = config->levels;
    }

    const uint32_t pulses = (internal->level - brightness + config->levels) % config->levels;
    const error_t error = send_pulses(internal, pulses);
    internal->level = (error == ERROR_NONE) ? brightness : LEVEL_UNKNOWN;
    return error;
}

static error_t pulse_count_backlight_set_brightness(Device* device, uint8_t brightness) {
    auto* internal = GET_INTERNAL(device);
    mutex_lock(&internal->mutex);
    const error_t error = set_brightness_locked(device, brightness);
    mutex_unlock(&internal->mutex);
    return error;
}

static error_t pulse_count_backlight_set_brightness_default(Device* device) {
    return pulse_count_backlight_set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t pulse_count_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    auto* internal = GET_INTERNAL(device);
    mutex_lock(&internal->mutex);
    const uint16_t level = internal->level;
    mutex_unlock(&internal->mutex);
    *out_brightness = (level == LEVEL_UNKNOWN) ? 0 : static_cast<uint8_t>(level);
    return ERROR_NONE;
}

static uint8_t pulse_count_backlight_get_max_brightness(Device* device) {
    return GET_CONFIG(device)->levels;
}

// endregion

static constexpr BacklightApi PULSE_COUNT_BACKLIGHT_API = {
    .set_brightness = pulse_count_backlight_set_brightness,
    .set_brightness_default = pulse_count_backlight_set_brightness_default,
    .get_brightness = pulse_count_backlight_get_brightness,
    .get_min_brightness = nullptr,
    .get_max_brightness = pulse_count_backlight_get_max_brightness,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (config->levels == 0) {
        LOG_E(TAG, "levels must be at least 1");
        return ERROR_INVALID_ARGUMENT;
    }

    auto* descriptor = gpio_descriptor_acquire(config->pin.gpio_controller, config->pin.pin, config->pin.flags | GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptor");
        return ERROR_RESOURCE;
    }

    auto* internal = new(std::nothrow) PulseCountBacklightInternal { .descriptor = descriptor, .mutex = {}, .level = LEVEL_UNKNOWN };
    if (internal == nullptr) {
        gpio_descriptor_release(descriptor);
        return ERROR_OUT_OF_MEMORY;
    }
    mutex_construct(&internal->mutex);
    if (internal->mutex.handle == nullptr) {
        gpio_descriptor_release(descriptor);
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    mutex_lock(&internal->mutex);
    turn_off(internal); // Allowed to fail, we don't care about the result
    mutex_unlock(&internal->mutex);
    gpio_descriptor_release(internal->descriptor);
    device_set_driver_data(device, nullptr);
    mutex_destruct(&internal->mutex);
    delete internal;
    return ERROR_NONE;
}

// endregion

Driver pulse_count_backlight_driver = {
    .name = "pulse_count_backlight",
    .compatible = (const char*[]) { "pulse-count-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &PULSE_COUNT_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .internal = nullptr,
};

}
