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
 * @brief API for relative pointing devices such as a Bluetooth or USB mouse.
 * Reports raw movement. Cursor position and scaling are a consumer concern (see lvgl/devices/mouse.h).
 */
struct MouseApi {
    /** Number of buttons the mouse has. Valid indices for get_button_pressed() are 0 to button_count - 1, where 0 is the primary button. */
    uint8_t button_count;

    /**
     * @brief Reads the accumulated movement since the last read, then resets it to zero.
     * @param[in] device the mouse device
     * @param[out] out_dx horizontal movement (right positive), in pixels
     * @param[out] out_dy vertical movement (down positive), in pixels
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*read_delta)(struct Device* device, int32_t* out_dx, int32_t* out_dy);

    /**
     * @brief Gets whether a button is currently pressed.
     * @param[in] device the mouse device
     * @param[in] index the button index, below button_count
     * @param[out] out_pressed true when pressed
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_button_pressed)(struct Device* device, uint8_t index, bool* out_pressed);
};

/**
 * @brief Reads the accumulated movement using the specified mouse device.
 */
error_t mouse_read_delta(struct Device* device, int32_t* out_dx, int32_t* out_dy);

/**
 * @brief Gets the number of buttons of the specified mouse device.
 */
uint8_t mouse_get_button_count(struct Device* device);

/**
 * @brief Gets whether a button is currently pressed on the specified mouse device.
 * @retval ERROR_INVALID_ARGUMENT when index is not below the button count
 */
error_t mouse_get_button_pressed(struct Device* device, uint8_t index, bool* out_pressed);

extern const struct DeviceType MOUSE_TYPE;

#ifdef __cplusplus
}
#endif
