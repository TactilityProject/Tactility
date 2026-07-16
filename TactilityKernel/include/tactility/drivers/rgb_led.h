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
 * @brief An RGB color value.
 */
struct RgbLedColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/**
 * @brief API for RGB LED drivers.
 */
struct RgbLedApi {
    /**
     * @brief Sets the LED color.
     * @param[in] device the RGB LED device
     * @param[in] color the color to set
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_color)(struct Device* device, const struct RgbLedColor color);

    /**
     * @brief Gets the LED color.
     * @param[in] device the RGB LED device
     * @param[out] out_color the current color
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_color)(struct Device* device, struct RgbLedColor* out_color);

    /**
     * @brief Enables the LED output.
     * @param[in] device the RGB LED device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*enable)(struct Device* device);

    /**
     * @brief Disables the LED output.
     * @param[in] device the RGB LED device
     */
    void (*disable)(struct Device* device);
};

/**
 * @brief Sets the LED color using the specified RGB LED device.
 */
error_t rgb_led_set_color(struct Device* device, struct RgbLedColor color);

/**
 * @brief Gets the LED color using the specified RGB LED device.
 */
error_t rgb_led_get_color(struct Device* device, struct RgbLedColor* out_color);

/**
 * @brief Enables the LED output using the specified RGB LED device.
 */
error_t rgb_led_enable(struct Device* device);

/**
 * @brief Disables the LED output using the specified RGB LED device.
 */
void rgb_led_disable(struct Device* device);

extern const struct DeviceType RGB_LED_TYPE;

#ifdef __cplusplus
}
#endif
