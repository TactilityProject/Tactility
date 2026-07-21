// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Inclusive range of brightness levels accepted by a backlight driver.
 */
struct BrightnessLevelRange {
    uint8_t min;
    uint8_t max;
};

/**
 * @brief API for backlight drivers.
 *
 * @note The minimum brightness level (see get_min_brightness()) turns the backlight fully off.
 * There is no separate on/off control: to turn the backlight off, call set_brightness() with
 * the minimum value; to turn it back on, set any value above the minimum.
 */
struct BacklightApi {
    /**
     * @brief Sets the backlight brightness.
     * @param[in] device the backlight device
     * @param[in] brightness the brightness level, expected to be within [get_min_brightness(), get_max_brightness()]
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_brightness)(struct Device* device, uint8_t brightness);

    /**
     * @brief Sets the backlight brightness to its devicetree-configured default level.
     * @param[in] device the backlight device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_brightness_default)(struct Device* device);

    /**
     * @brief Gets the current backlight brightness.
     * @param[in] device the backlight device
     * @param[out] out_brightness the current brightness level
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_brightness)(struct Device* device, uint8_t* out_brightness);

    /**
     * @brief Gets the minimum brightness level. Setting the brightness to this value turns the backlight off.
     * @param[in] device the backlight device
     * @return the minimum brightness level
     */
    uint8_t (*get_min_brightness)(struct Device* device);

    /**
     * @brief Gets the maximum (full-strength) brightness level.
     * @param[in] device the backlight device
     * @return the maximum brightness level
     */
    uint8_t (*get_max_brightness)(struct Device* device);
};

/**
 * @brief Sets the backlight brightness using the specified backlight device.
 */
error_t backlight_set_brightness(struct Device* device, uint8_t brightness);

/**
 * @brief Sets the backlight brightness to its devicetree-configured default level using the specified backlight device.
 */
error_t backlight_set_brightness_default(struct Device* device);

/**
 * @brief Gets the current backlight brightness using the specified backlight device.
 */
error_t backlight_get_brightness(struct Device* device, uint8_t* out_brightness);

/**
 * @brief Gets the minimum brightness level using the specified backlight device. This level turns the backlight off.
 */
uint8_t backlight_get_min_brightness(struct Device* device);

/**
 * @brief Gets the maximum brightness level using the specified backlight device.
 */
uint8_t backlight_get_max_brightness(struct Device* device);

extern const struct DeviceType BACKLIGHT_TYPE;

#ifdef __cplusplus
}
#endif
