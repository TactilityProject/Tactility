// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief A single key event read from a keyboard device.
 */
struct KeyboardKeyData {
    /** @brief The key code. Driver-defined (e.g. ASCII/UTF-8 codepoint, scan code, or LVGL key code). */
    uint32_t key;
    /** @brief True if the key was pressed, false if released. */
    bool pressed;
    /**
     * @brief True if another key event is already queued and read_key() should be called again
     * immediately to drain it. False if this was the last pending event.
     */
    bool continue_reading;
};

/**
 * @brief API for keyboard drivers.
 */
struct KeyboardApi {
    /**
     * @brief Reads the next pending key event, if any.
     * @param[in] device the keyboard device
     * @param[out] data the key event data
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*read_key)(struct Device* device, struct KeyboardKeyData* data);

    /**
     * @brief Returns the baclight if the keyboard has one.
     * @param[in] device the keyboard device
     * @param[out] backlight_device the output backlight device
     * @retval ERROR_NONE when the backlight_device was set
     * @retval ERROR_NOT_SUPPORTED when this device has no backlight
     */
    error_t (*get_backlight)(struct Device* device, struct Device** backlight_device);
};

/**
 * @brief Reads the next pending key event using the specified keyboard device.
 */
error_t keyboard_read_key(struct Device* device, struct KeyboardKeyData* data);

/**
 * @brief Returns the backlight if the keyboard has one.
 * @param[in] device the keyboard device
 * @param[out] backlight_device the output backlight device
 * @retval ERROR_NONE when the backlight_device was set
 * @retval ERROR_NOT_SUPPORTED when this device has no backlight
 */
error_t keyboard_get_backlight(struct Device* device, struct Device** backlight_device);


extern const struct DeviceType KEYBOARD_TYPE;

#ifdef __cplusplus
}
#endif
