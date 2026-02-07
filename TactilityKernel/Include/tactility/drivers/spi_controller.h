// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include <tactility/freertos/freertos.h>
#include <tactility/error.h>

/**
 * @brief API for SPI controller drivers.
 */
struct SpiControllerApi {
    /**
     * @brief Resets the SPI controller.
     * @param[in] device the SPI controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*reset)(struct Device* device);

    /**
     * @brief Locks the SPI controller.
     * @param[in] device the SPI controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*lock)(struct Device* device);

    /**
     * @brief Tries to lock the SPI controller.
     * @param[in] device the SPI controller device
     * @param[in] timeout the maximum time to wait for the lock
     * @retval ERROR_NONE when the operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*try_lock)(struct Device* device, TickType_t timeout);

    /**
     * @brief Unlocks the SPI controller.
     * @param[in] device the SPI controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*unlock)(struct Device* device);
};

/**
 * @brief Resets the SPI controller using the specified controller.
 * @param[in] device the SPI controller device
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_reset(struct Device* device);

/**
 * @brief Locks the SPI controller using the specified controller.
 * @param[in] device the SPI controller device
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_lock(struct Device* device);

/**
 * @brief Tries to lock the SPI controller using the specified controller.
 * @param[in] device the SPI controller device
* @param[in] timeout the maximum time to wait for the lock
 * @retval ERROR_NONE when the operation was successful
 * @retval ERROR_TIMEOUT when the operation timed out
 */
error_t spi_controller_try_lock(struct Device* device, TickType_t timeout);

/**
 * @brief Unlocks the SPI controller using the specified controller.
 * @param[in] device the SPI controller device
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_unlock(struct Device* device);

extern const struct DeviceType SPI_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
