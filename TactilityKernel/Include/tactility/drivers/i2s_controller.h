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

/**
 * @brief I2S Mode
 */
typedef enum {
    I2S_MODE_MASTER = 1,
    I2S_MODE_SLAVE = 2,
} i2s_mode_t;

/**
 * @brief I2S Communication Format
 */
typedef enum {
    I2S_COMM_FORMAT_STAND_I2S = 0x01,
    I2S_COMM_FORMAT_STAND_MSB = 0x02,
    I2S_COMM_FORMAT_STAND_PCM_SHORT = 0x04,
    I2S_COMM_FORMAT_STAND_PCM_LONG = 0x08,
} i2s_comm_format_t;

/**
 * @brief I2S Channel Format
 */
typedef enum {
    I2S_CHANNEL_FMT_RIGHT_LEFT = 0,
    I2S_CHANNEL_FMT_ALL_RIGHT,
    I2S_CHANNEL_FMT_ALL_LEFT,
    I2S_CHANNEL_FMT_ONLY_RIGHT,
    I2S_CHANNEL_FMT_ONLY_LEFT,
} i2s_channel_fmt_t;

/**
 * @brief I2S Bits Per Sample
 */
typedef enum {
    I2S_BITS_PER_SAMPLE_16BIT = 16,
    I2S_BITS_PER_SAMPLE_24BIT = 24,
    I2S_BITS_PER_SAMPLE_32BIT = 32,
} i2s_bits_per_sample_t;

/**
 * @brief I2S Config
 */
struct I2sConfig {
    i2s_mode_t mode;
    uint32_t sampleRate;
    i2s_bits_per_sample_t bitsPerSample;
    i2s_channel_fmt_t channelFormat;
    i2s_comm_format_t communicationFormat;
};

/**
 * @brief API for I2S controller drivers.
 */
struct I2sControllerApi {
    /**
     * @brief Reads data from I2S.
     * @param[in] device the I2S controller device
     * @param[out] data the buffer to store the read data
     * @param[in] dataSize the number of bytes to read
     * @param[out] bytesRead the number of bytes actually read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the read operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read)(struct Device* device, void* data, size_t dataSize, size_t* bytesRead, TickType_t timeout);

    /**
     * @brief Writes data to I2S.
     * @param[in] device the I2S controller device
     * @param[in] data the buffer containing the data to write
     * @param[in] dataSize the number of bytes to write
     * @param[out] bytesWritten the number of bytes actually written
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the write operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write)(struct Device* device, const void* data, size_t dataSize, size_t* bytesWritten, TickType_t timeout);

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
};

/**
 * @brief Reads data from I2S using the specified controller.
 * @param[in] device the I2S controller device
 * @param[out] data the buffer to store the read data
 * @param[in] dataSize the number of bytes to read
 * @param[out] bytesRead the number of bytes actually read
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the read operation was successful
 */
error_t i2s_controller_read(struct Device* device, void* data, size_t dataSize, size_t* bytesRead, TickType_t timeout);

/**
 * @brief Writes data to I2S using the specified controller.
 * @param[in] device the I2S controller device
 * @param[in] data the buffer containing the data to write
 * @param[in] dataSize the number of bytes to write
 * @param[out] bytesWritten the number of bytes actually written
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the write operation was successful
 */
error_t i2s_controller_write(struct Device* device, const void* data, size_t dataSize, size_t* bytesWritten, TickType_t timeout);

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

extern const struct DeviceType I2S_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
