// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>

/**
 * @brief A single ladder button: an ADC channel plus the raw-value band that selects it.
 */
struct AdcButtonControlConfig {
    /** ADC device controlling the channel */
    struct Device* adc_controller;
    /** The channel's index on the device */
    uint8_t channel;
    /** Upper (inclusive) edge of the raw-value band; no button above this value */
    int band_high;
    /** Lower (exclusive) edge of the raw-value band; INT32_MIN for the bottom band */
    int band_low;
    /** LVGL key code emitted when the button is pressed */
    uint32_t key;
};

struct ButtonAdcControlConfig {
    /** One entry per button, in declared order */
    struct AdcButtonControlConfig* buttons;
    /** The item count of buttons */
    uint32_t buttons_count;
    /** Minimum time between recognized state changes, for software debouncing */
    uint32_t debounce_ms;
};

#ifdef __cplusplus
}
#endif
