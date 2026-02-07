// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "gpio.h"

#include <tactility/freertos/freertos.h>
#include <tactility/error.h>

struct Device;

/**
 * @brief I2S communication format
 */
enum I2sCommunicationFormat {
    I2S_FORMAT_STAND_I2S = 0x01,
    I2S_FORMAT_STAND_MSB = 0x02,
    I2S_FORMAT_STAND_PCM_SHORT = 0x04,
    I2S_FORMAT_STAND_PCM_LONG = 0x08,
};

#define I2S_CHANNEL_NONE -1

/**
 * @brief I2S Config
 */
struct I2sConfig {
    enum I2sCommunicationFormat communication_format;
    uint32_t sample_rate;
    uint8_t bits_per_sample; // 16, 24, 32
    int8_t channel_left; // I2S_CHANNEL_NONE to disable
    int8_t channel_right; // I2S_CHANNEL_NONE to disable
};

/**
 * @brief API for I2S controller drivers.
 */
struct I2sControllerApi {
    /**
     * @brief Reads data from I2S.
     * @param[in] device the I2S controller device
     * @param[out] data the buffer to store the read data
     * @param[in] data_size the number of bytes to read
     * @param[out] bytes_read the number of bytes actually read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the read operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read)(struct Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout);

    /**
     * @brief Writes data to I2S.
     * @param[in] device the I2S controller device
     * @param[in] data the buffer containing the data to write
     * @param[in] data_size the number of bytes to write
     * @param[out] bytes_written the number of bytes actually written
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the write operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write)(struct Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout);

    /**
     * @brief Sets the I2S configuration.
     * @param[in] device the I2S controller device
     * @param[in] config the new configuration
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_config)(struct Device* device, const struct I2sConfig* config);

    /**
     * @brief Gets the current I2S configuration.
     * @param[in] device the I2S controller device
     * @param[out] config the buffer to store the current configuration
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_config)(struct Device* device, struct I2sConfig* config);

    /**
     * @brief Resets the I2S controller. Must configure it again before it can be used.
     * @param[in] device the I2S controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*reset)(struct Device* device);
};

/**
 * @brief Reads data from I2S using the specified controller.
 * @param[in] device the I2S controller device
 * @param[out] data the buffer to store the read data
 * @param[in] data_size the number of bytes to read
 * @param[out] bytes_read the number of bytes actually read
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the read operation was successful
 */
error_t i2s_controller_read(struct Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout);

/**
 * @brief Writes data to I2S using the specified controller.
 * @param[in] device the I2S controller device
 * @param[in] data the buffer containing the data to write
 * @param[in] data_size the number of bytes to write
 * @param[out] bytes_written the number of bytes actually written
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the write operation was successful
 */
error_t i2s_controller_write(struct Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout);

/**
 * @brief Sets the I2S configuration using the specified controller.
 * @param[in] device the I2S controller device
 * @param[in] config the new configuration
 * @retval ERROR_NONE when the operation was successful
 */
error_t i2s_controller_set_config(struct Device* device, const struct I2sConfig* config);

/**
 * @brief Gets the current I2S configuration using the specified controller.
 * @param[in] device the I2S controller device
 * @param[out] config the buffer to store the current configuration
 * @retval ERROR_NONE when the operation was successful
 */
error_t i2s_controller_get_config(struct Device* device, struct I2sConfig* config);

/**
 * @brief Resets the I2S controller. Must configure it again before it can be used.
 * @param[in] device the I2S controller device
 * @retval ERROR_NONE when the operation was successful
 */
error_t i2s_controller_reset(struct Device* device);

extern const struct DeviceType I2S_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
