// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>
#include <tactility/error.h>
#include <stdbool.h>
#include <stdint.h>

struct Device;
struct DeviceType;

struct TdeckTrackballConfig {
    struct GpioPinSpec pin_right;
    struct GpioPinSpec pin_up;
    struct GpioPinSpec pin_left;
    struct GpioPinSpec pin_down;
    struct GpioPinSpec pin_click;
};

/**
 * @brief API for the T-Deck 5-way trackball driver.
 * Reports raw, unscaled movement: sensitivity and mode (encoder vs. pointer) are UI concerns
 * layered on top by the consumer, not something this driver knows about.
 */
struct TdeckTrackballApi {
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
};

/**
 * @brief Reads the accumulated movement using the specified trackball device.
 */
error_t tdeck_trackball_read_delta(struct Device* device, int32_t* out_dx, int32_t* out_dy);

/**
 * @brief Gets whether the click button is currently pressed on the specified trackball device.
 */
error_t tdeck_trackball_get_button_pressed(struct Device* device, bool* out_pressed);

extern const struct DeviceType TDECK_TRACKBALL_TYPE;

#ifdef __cplusplus
}
#endif
