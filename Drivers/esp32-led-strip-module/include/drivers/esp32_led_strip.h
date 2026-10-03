// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/led_strip.h>

#include <esp32_led_strip_types.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct LedStripConfig {
    /** Data Pin */
    struct GpioPinSpec pin_data;

    /** LED Count */
    uint16_t led_count;
};

/**
 * @brief Sets the color of a single LED on the strip with global brightness.
 * @param[in] device the LED strip device
 * @param[in] index index of the LED in the strip (0-based)
 * @param[in] color the color to set
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_set_single_led_color(struct Device* device, size_t index, const struct LedRgb color);

/**
 * @brief Sets the color of a single LED on the strip with an explicit brightness value.
 * @param[in] device the LED strip device
 * @param[in] index index of the LED in the strip (0-based)
 * @param[in] color the color to set
 * @param[in] brightness 0-255
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_set_single_led_color_and_brightness(struct Device* device, size_t index, const struct LedRgb color, uint8_t brightness);

/**
 * @brief Sets colors for a range of LEDs on the strip using the global brightness.
 * @param[in] device the LED strip device
 * @param[in] first starting LED index (0-based)
 * @param[in] count number of LEDs to set
 * @param[in] color array of colors to set
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_set_led_range_color(struct Device* device, size_t first, size_t count, const struct LedRgb* color);

/**
 * @brief Sets colors for a range of LEDs on the strip with an explicit brightness value.
 * @param[in] device the LED strip device
 * @param[in] first starting LED index (0-based)
 * @param[in] count number of LEDs to set
 * @param[in] color array of colors to set
 * @param[in] brightness 0-255
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_set_led_range_color_and_brightness(struct Device* device, size_t first, size_t count, const struct LedRgb* color, uint8_t brightness);

/**
 * @brief Fills a range of LEDs with a single color using the global brightness.
 * @param[in] device the LED strip device
 * @param[in] first starting LED index (0-based)
 * @param[in] count number of LEDs to fill
 * @param[in] color the color to fill with
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_fill_led_range_color(struct Device* device, size_t first, size_t count, const struct LedRgb color);

/**
 * @brief Fills a range of LEDs with a single color and an explicit brightness value.
 * @param[in] device the LED strip device
 * @param[in] first starting LED index (0-based)
 * @param[in] count number of LEDs to fill
 * @param[in] color the color to fill with
 * @param[in] brightness 0-255
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_fill_led_range_color_and_brightness(struct Device* device, size_t first, size_t count, const struct LedRgb color, uint8_t brightness);

/**
 * @brief Sets global brightness for the LED strip.
 * @param[in] device the LED strip device
 * @param[in] brightness 0-255
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_set_brightness(struct Device* device, uint8_t brightness);

/**
 * @brief Refreshes the strip to update the displayed colors.
 * @param[in] device the LED strip device
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_show(struct Device* device);

/**
 * @brief Clears the strip to black.
 * @param[in] device the LED strip device
 * @retval ERROR_NONE when the operation was successful
 */
error_t esp32_led_strip_clear(struct Device* device);

/**
 * @brief Gets the length of a strip.
 * @param[in] device the LED strip device
 * @param[out] out_length receives the strip length
 * @retval ERROR_NONE when the operation was successful
 * @retval ERROR_INVALID_ARGUMENT if out of range
 */
error_t esp32_led_strip_get_length(struct Device* device, uint16_t* out_length);

#ifdef __cplusplus
}
#endif
