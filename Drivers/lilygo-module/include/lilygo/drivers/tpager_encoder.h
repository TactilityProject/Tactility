// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>
#include <tactility/error.h>
#include <stdbool.h>
#include <stdint.h>

struct Device;
struct DeviceType;

struct TpagerEncoderConfig {
    struct GpioPinSpec pin_a;
    struct GpioPinSpec pin_b;
    struct GpioPinSpec pin_enter;
};

/**
 * @brief API for the T-Lora Pager encoder wheel driver.
 * Reports raw, unscaled pulses: pulses-per-detent scaling and enter-press debounce are UI
 * concerns layered on top by the consumer, not something this driver knows about.
 */
struct TpagerEncoderApi {
    /**
     * @brief Reads the accumulated pulse count since the last read, then resets it to zero.
     * @param[in] device the encoder device
     * @param[out] out_pulses accumulated quadrature pulses (positive/negative by direction)
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*read_delta)(struct Device* device, int32_t* out_pulses);

    /**
     * @brief Gets whether the enter button is currently pressed.
     * @param[in] device the encoder device
     * @param[out] out_pressed true when pressed
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_button_pressed)(struct Device* device, bool* out_pressed);
};

/**
 * @brief Reads the accumulated pulse count using the specified encoder device.
 */
error_t tpager_encoder_read_delta(struct Device* device, int32_t* out_pulses);

/**
 * @brief Gets whether the enter button is currently pressed on the specified encoder device.
 */
error_t tpager_encoder_get_button_pressed(struct Device* device, bool* out_pressed);

extern const struct DeviceType TPAGER_ENCODER_TYPE;

#ifdef __cplusplus
}
#endif
