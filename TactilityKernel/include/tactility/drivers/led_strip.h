// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An RGB color value.
 */
struct LedRgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

#define LED_RGB_COLOR_WHITE   ((struct LedRgb){255, 255, 255})
#define LED_RGB_COLOR_GREEN   ((struct LedRgb){0, 255, 0})
#define LED_RGB_COLOR_RED     ((struct LedRgb){255, 0, 0})
#define LED_RGB_COLOR_BLUE    ((struct LedRgb){0, 0, 255})
#define LED_RGB_COLOR_YELLOW  ((struct LedRgb){255, 255, 0})
#define LED_RGB_COLOR_ORANGE  ((struct LedRgb){255, 165, 0})
#define LED_RGB_COLOR_CYAN    ((struct LedRgb){0, 255, 255})
#define LED_RGB_COLOR_MAGENTA ((struct LedRgb){255, 0, 255})
#define LED_RGB_COLOR_PURPLE  ((struct LedRgb){191, 0, 255})
#define LED_RGB_COLOR_PINK    ((struct LedRgb){255, 192, 203})
#define LED_RGB_COLOR_TEAL    ((struct LedRgb){0, 128, 128})
#define LED_RGB_COLOR_BLACK   ((struct LedRgb){0, 0, 0})

#define LED_COLOR_RGB_CUSTOM(r, g, b) ((struct LedRgb){(r), (g), (b)})

struct LedStripApi {
    error_t (*set_single_led_color)(struct Device* device, size_t index, const struct LedRgb color);
    error_t (*set_single_led_color_and_brightness)(struct Device* device, size_t index, const struct LedRgb color, uint8_t brightness);

    error_t (*set_led_range_color)(struct Device* device, size_t first, size_t count, const struct LedRgb* color);
    error_t (*set_led_range_color_and_brightness)(struct Device* device, size_t first, size_t count, const struct LedRgb* color, uint8_t brightness);

    error_t (*fill_led_range_color)(struct Device* device, size_t first, size_t count, const struct LedRgb color);
    error_t (*fill_led_range_color_and_brightness)(struct Device* device, size_t first, size_t count, const struct LedRgb color, uint8_t brightness);

    error_t (*set_brightness)(struct Device* device, uint8_t brightness);

    error_t (*show)(struct Device* device);

    error_t (*clear)(struct Device* device);

    error_t (*get_length)(struct Device* device, uint16_t* out_length);
};

extern const struct DeviceType LED_STRIP_TYPE;

/**
 * @brief Sets the color of a single LED using the global brightness.
 */
error_t led_strip_set_single_led_color(struct Device* device, size_t index, const struct LedRgb color);

/**
 * @brief Sets the color of a single LED with an explicit brightness value.
 */
error_t led_strip_set_single_led_color_and_brightness(struct Device* device, size_t index, const struct LedRgb color, uint8_t brightness);

/**
 * @brief Sets colors for a range of LEDs using the global brightness.
 */
error_t led_strip_set_led_range_color(struct Device* device, size_t first, size_t count, const struct LedRgb* color);

/**
 * @brief Sets colors for a range of LEDs with an explicit brightness value.
 */
error_t led_strip_set_led_range_color_and_brightness(struct Device* device, size_t first, size_t count, const struct LedRgb* color, uint8_t brightness);

/**
 * @brief Fills a range of LEDs with a single color using the global brightness.
 */
error_t led_strip_fill_led_range_color(struct Device* device, size_t first, size_t count, const struct LedRgb color);

/**
 * @brief Fills a range of LEDs with a single color and an explicit brightness value.
 */
error_t led_strip_fill_led_range_color_and_brightness(struct Device* device, size_t first, size_t count, const struct LedRgb color, uint8_t brightness);

/**
 * @brief Set global brightness for the LED strip.
 */
error_t led_strip_set_brightness(struct Device* device, uint8_t brightness);

/**
 * @brief Refreshes the strip to update the displayed colors.
 */
error_t led_strip_show(struct Device* device);

/**
 * @brief Clears the strip to black.
 */
error_t led_strip_clear(struct Device* device);

/**
 * @brief Get the LED strip length.
 */
error_t led_strip_get_length(struct Device* device, uint16_t* out_length);

#ifdef __cplusplus
}
#endif