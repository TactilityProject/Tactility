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
 * @brief I2S TDM RX config (e.g. for ES7210 4-slot microphone ADC)
 */
struct I2sTdmRxConfig {
    uint32_t sample_rate_hz;
    uint32_t mclk_multiple;   // e.g. 256 → I2S_MCLK_MULTIPLE_256
    uint8_t  bclk_div;        // e.g. 8; must not be 0
    uint8_t  slot_count;      // number of TDM slots, e.g. 4; valid range: 1–16
    uint8_t  bits_per_sample; // 16, 24, or 32
    uint8_t  slot_bit_width;  // bit width of each TDM slot (0 = auto, matches bits_per_sample)
};

/**
 * @brief I2S PDM RX config (e.g. for a PDM MEMS microphone such as the SPM1423).
 * Only supported on I2S controller 0 on ESP32 targets -- PDM RX is hardware-restricted
 * to that controller. PDM only supports 16-bit samples and mono or stereo.
 */
struct I2sPdmRxConfig {
    uint32_t sample_rate_hz;
    bool     stereo; // false = mono (single PDM mic), true = stereo (two PDM mics)
};

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

    /**
     * @brief Reconfigures the RX channel to TDM mode (e.g. for ES7210).
     * Does not require set_config() to be called first -- TDM allocates its own RX
     * channel handle independently of standard mode (calling set_config() first would
     * leave its TX/RX channel pair bound to the shared BCLK/WS/MCLK pins, which then
     * blocks this call from claiming them; callers should pick TDM or standard mode
     * up front, not call both in sequence).
     * @param[in] device the I2S controller device
     * @param[in] config TDM parameters
     * @retval ERROR_NONE when the operation was successful
     * @retval ERROR_NOT_SUPPORTED if the driver does not implement TDM
     */
    error_t (*set_rx_tdm_config)(struct Device* device, const struct I2sTdmRxConfig* config);

    /**
     * @brief Reconfigures the RX channel to PDM mode (e.g. for a PDM MEMS microphone).
     * Does not require set_config() to be called first -- PDM RX allocates its own
     * channel handle independently of standard/TDM mode.
     * @param[in] device the I2S controller device
     * @param[in] config PDM parameters
     * @retval ERROR_NONE when the operation was successful
     * @retval ERROR_NOT_SUPPORTED if the driver/controller does not support PDM RX
     */
    error_t (*set_rx_pdm_config)(struct Device* device, const struct I2sPdmRxConfig* config);

    /**
     * @brief Disables and releases only the TX or only the RX channel, leaving the other
     * direction (if active) running. Unlike reset(), which tears down both directions --
     * unsuitable when a single controller carries an independent TX user (e.g. a speaker
     * amp) and RX user (e.g. a PDM mic) that must be able to close without affecting each
     * other.
     * @param[in] device the I2S controller device
     * @param[in] is_input true to disable the RX channel, false to disable the TX channel
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*disable_direction)(struct Device* device, bool is_input);
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

/**
 * @brief Reconfigures the RX channel to TDM mode (e.g. for ES7210 4-slot mic ADC).
 * Does not require i2s_controller_set_config() to be called first -- see
 * I2sControllerApi::set_rx_tdm_config for why standard mode and TDM mode are
 * alternative setup paths rather than sequential steps.
 * @param[in] device the I2S controller device
 * @param[in] config TDM parameters
 * @retval ERROR_NONE when the operation was successful
 * @retval ERROR_NOT_SUPPORTED if the driver does not implement TDM
 */
error_t i2s_controller_set_rx_tdm_config(struct Device* device, const struct I2sTdmRxConfig* config);

/**
 * @brief Reconfigures the RX channel to PDM mode (e.g. for a PDM MEMS microphone).
 * @param[in] device the I2S controller device
 * @param[in] config PDM parameters
 * @retval ERROR_NONE when the operation was successful
 * @retval ERROR_NOT_SUPPORTED if the driver/controller does not support PDM RX
 */
error_t i2s_controller_set_rx_pdm_config(struct Device* device, const struct I2sPdmRxConfig* config);

/**
 * @brief See I2sControllerApi::disable_direction. Falls back to the full reset() if the
 * driver doesn't implement direction-aware teardown.
 * @param[in] device the I2S controller device
 * @param[in] is_input true to disable the RX channel, false to disable the TX channel
 * @retval ERROR_NONE when the operation was successful
 */
error_t i2s_controller_disable_direction(struct Device* device, bool is_input);

extern const struct DeviceType I2S_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
