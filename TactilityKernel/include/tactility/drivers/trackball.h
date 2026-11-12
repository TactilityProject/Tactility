// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief What a trackball presents itself as.
 */
enum TrackballMode {
    /** Arrow keys for movement and enter for the button, through a child device of type KEYBOARD_TYPE. */
    TRACKBALL_MODE_KEYBOARD,
    /** Cursor movement and a button, through a child device of type MOUSE_TYPE. */
    TRACKBALL_MODE_MOUSE
};

/**
 * @brief Runtime-adjustable trackball behavior.
 */
struct TrackballSettings {
    enum TrackballMode mode;
    /** When false, the trackball has no child device and its input is discarded. */
    bool enabled;
    /** Keyboard mode: arrow key presses per raw trackball tick. Must be >= 1. */
    uint8_t key_sensitivity;
    /** Mouse mode: pixels per raw trackball tick. Must be >= 1. */
    uint8_t pointer_sensitivity;
};

/**
 * @brief API for trackball drivers.
 * A trackball device reports raw, unscaled movement. Its driver exposes the input as a child device
 * of type KEYBOARD_TYPE or MOUSE_TYPE, depending on the settings.
 */
struct TrackballApi {
    /**
     * @brief Reads the accumulated movement since the last read, then resets it to zero.
     * @param[in] device the trackball device
     * @param[out] out_dx horizontal movement (right positive), in raw pulses
     * @param[out] out_dy vertical movement (down positive), in raw pulses
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*read_delta)(struct Device* device, int32_t* out_dx, int32_t* out_dy);

    /**
     * @brief Gets whether the click button is currently pressed.
     * @param[in] device the trackball device
     * @param[out] out_pressed true when pressed
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_button_pressed)(struct Device* device, bool* out_pressed);

    /**
     * @brief Applies settings. Switching mode or enabled state replaces the child device.
     * @param[in] device the trackball device
     * @param[in] settings the settings to apply
     * @retval ERROR_NONE when the operation was successful
     * @retval ERROR_INVALID_ARGUMENT when the mode is unknown or a sensitivity is 0
     */
    error_t (*set_settings)(struct Device* device, const struct TrackballSettings* settings);

    /**
     * @brief Gets the settings that are currently active.
     * @param[in] device the trackball device
     * @param[out] out_settings the active settings
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_settings)(struct Device* device, struct TrackballSettings* out_settings);
};

/**
 * @brief Gets the default settings: keyboard mode, enabled, key_sensitivity 1, pointer_sensitivity 10.
 */
struct TrackballSettings trackball_settings_get_default(void);

/**
 * @brief Reads the accumulated movement using the specified trackball device.
 */
error_t trackball_read_delta(struct Device* device, int32_t* out_dx, int32_t* out_dy);

/**
 * @brief Gets whether the click button is currently pressed on the specified trackball device.
 */
error_t trackball_get_button_pressed(struct Device* device, bool* out_pressed);

/**
 * @brief Applies settings using the specified trackball device.
 */
error_t trackball_set_settings(struct Device* device, const struct TrackballSettings* settings);

/**
 * @brief Gets the active settings of the specified trackball device.
 */
error_t trackball_get_settings(struct Device* device, struct TrackballSettings* out_settings);

extern const struct DeviceType TRACKBALL_TYPE;

#ifdef __cplusplus
}
#endif
