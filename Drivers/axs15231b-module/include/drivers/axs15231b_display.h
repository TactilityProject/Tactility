// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Axs15231bDisplayConfig {
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;
    bool bgr_order;
    uint32_t pixel_clock_hz;
    uint32_t transaction_queue_depth;

    // Reset pin. GPIO_PIN_SPEC_NONE means no reset line is wired up (matches the original
    // deprecated-HAL config for the boards using this chip so far).
    struct GpioPinSpec pin_reset;
    bool reset_active_high;

    // Optional Tearing-Effect GPIO pin. When set (not GPIO_PIN_SPEC_NONE), draw_bitmap() waits
    // (best-effort, up to 20ms) for a V-blank pulse on this pin before starting each transfer, to
    // reduce visible tearing. GPIO_PIN_SPEC_NONE skips TE sync entirely.
    struct GpioPinSpec pin_te;

    // Custom vendor init sequence, flattened as bytes: a run of
    // [cmd, data_len, delay_ms, data_len bytes of data...] entries. NULL/0 falls back to the
    // AXS15231B component's own built-in default sequence.
    const uint8_t* init_sequence;
    uint32_t init_sequence_length;

    bool requires_full_frame;

    // Optional reference to this display's backlight device, NULL if none.
    struct Device* backlight;
};

#ifdef __cplusplus
}
#endif
