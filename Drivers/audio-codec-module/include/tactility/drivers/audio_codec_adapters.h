// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>
#include "audio_codec_data_if.h"
#include "audio_codec_ctrl_if.h"
#include "audio_codec_gpio_if.h"

struct Device;

/**
 * @brief Creates an esp_codec_dev data interface backed by a Tactility I2S controller device.
 * @param[in] i2s_controller the I2S controller device to read/write audio frames from/to
 * @return the data interface, or NULL on failure. Free with audio_codec_delete_data_if().
 */
const audio_codec_data_if_t* audio_codec_adapter_new_i2s_data(struct Device* i2s_controller);

/**
 * @brief Creates an esp_codec_dev data interface backed by a Tactility I2S controller device,
 * configured for PDM RX mode (e.g. for a PDM MEMS microphone such as the SPM1423).
 * @param[in] i2s_controller the I2S controller device to read PDM audio frames from. Must be
 *            backed by I2S controller 0 -- PDM RX is hardware-restricted to that controller.
 * @return the data interface, or NULL on failure. Free with audio_codec_delete_data_if().
 */
const audio_codec_data_if_t* audio_codec_adapter_new_i2s_pdm_data(struct Device* i2s_controller);

/**
 * @brief Creates an esp_codec_dev control interface backed by a Tactility I2C controller device.
 * @param[in] i2c_controller the I2C controller device the codec chip is attached to
 * @param[in] address the 7-bit I2C address of the codec chip
 * @return the control interface, or NULL on failure. Free with audio_codec_delete_ctrl_if().
 */
const audio_codec_ctrl_if_t* audio_codec_adapter_new_i2c_ctrl(struct Device* i2c_controller, uint8_t address);

/**
 * @brief Creates an esp_codec_dev GPIO interface backed by Tactility GpioPinSpec descriptors.
 *
 * The codec chip driver references GPIOs by small integer index (0, 1, ...); this adapter
 * maps those indices to GpioPinSpec entries supplied at creation time (e.g. reset/PA-enable lines
 * declared in devicetree). Indices outside the supplied array are treated as no-ops.
 *
 * @param[in] pins array of pin specs, indexed by the small integer GPIO id used by the codec chip driver
 * @note The caller must keep `pins` valid for the lifetime of the returned interface (until
 *       audio_codec_adapter_delete_gpio() is called); the adapter stores the pointer, not a copy.
 * @param[in] pin_count number of entries in `pins`
 * @note Only a single instance is supported at a time; a second call before deleting the
 *       first returns NULL.
 * @return the GPIO interface, or NULL on failure. Free with audio_codec_adapter_delete_gpio()
 *         (esp_codec_dev's generic audio_codec_delete_gpio_if() does not release our GPIO descriptors).
 */
const audio_codec_gpio_if_t* audio_codec_adapter_new_gpio(const struct GpioPinSpec* pins, size_t pin_count);

/**
 * @brief Frees a GPIO interface created by audio_codec_adapter_new_gpio(), releasing its GPIO descriptors.
 */
int audio_codec_adapter_delete_gpio(const audio_codec_gpio_if_t* gpio_if);

#ifdef __cplusplus
}
#endif
