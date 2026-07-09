// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>
#include <tactility/freertos/freertos.h>

/**
 * @brief API for touch controller drivers.
 */
struct TouchApi {
    /**
     * @brief Puts the touch controller into its low-power sleep mode.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*enter_sleep)(struct Device* device);

    /**
     * @brief Wakes the touch controller from sleep mode.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*exit_sleep)(struct Device* device);

    /**
     * @brief Performs a blocking read of the latest touch data from the controller into its internal state.
     * Call this before get_touched_points().
     * @param[in] device the touch device
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the read operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read_data)(struct Device* device, TickType_t timeout);

    /**
     * @brief Retrieves the coordinates most recently read by read_data().
     * @param[in] device the touch device
     * @param[out] x array of X coordinates
     * @param[out] y array of Y coordinates
     * @param[out] strength optional array of touch strengths (nullable)
     * @param[out] point_count the number of points currently touched
     * @param[in] max_point_count the maximum number of points the arrays can hold
     * @return true when touched and coordinates are available
     */
    bool (*get_touched_points)(struct Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count);

    /**
     * @brief Sets whether the X and Y axes should be swapped.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_swap_xy)(struct Device* device, bool swap);

    /**
     * @brief Gets whether the X and Y axes are swapped.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_swap_xy)(struct Device* device, bool* swap);

    /**
     * @brief Sets whether the X axis should be mirrored.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_mirror_x)(struct Device* device, bool mirror);

    /**
     * @brief Gets whether the X axis is mirrored.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_mirror_x)(struct Device* device, bool* mirror);

    /**
     * @brief Sets whether the Y axis should be mirrored.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_mirror_y)(struct Device* device, bool mirror);

    /**
     * @brief Gets whether the Y axis is mirrored.
     * @param[in] device the touch device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_mirror_y)(struct Device* device, bool* mirror);
};

/**
 * @brief Puts the touch controller into its low-power sleep mode using the specified touch device.
 */
error_t touch_enter_sleep(struct Device* device);

/**
 * @brief Wakes the touch controller from sleep mode using the specified touch device.
 */
error_t touch_exit_sleep(struct Device* device);

/**
 * @brief Performs a blocking read of the latest touch data using the specified touch device.
 */
error_t touch_read_data(struct Device* device, TickType_t timeout);

/**
 * @brief Retrieves the coordinates most recently read using the specified touch device.
 */
bool touch_get_touched_points(struct Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count);

/**
 * @brief Sets whether the X and Y axes should be swapped using the specified touch device.
 */
error_t touch_set_swap_xy(struct Device* device, bool swap);

/**
 * @brief Gets whether the X and Y axes are swapped using the specified touch device.
 */
error_t touch_get_swap_xy(struct Device* device, bool* swap);

/**
 * @brief Sets whether the X axis should be mirrored using the specified touch device.
 */
error_t touch_set_mirror_x(struct Device* device, bool mirror);

/**
 * @brief Gets whether the X axis is mirrored using the specified touch device.
 */
error_t touch_get_mirror_x(struct Device* device, bool* mirror);

/**
 * @brief Sets whether the Y axis should be mirrored using the specified touch device.
 */
error_t touch_set_mirror_y(struct Device* device, bool mirror);

/**
 * @brief Gets whether the Y axis is mirrored using the specified touch device.
 */
error_t touch_get_mirror_y(struct Device* device, bool* mirror);

extern const struct DeviceType TOUCH_TYPE;

#ifdef __cplusplus
}
#endif
