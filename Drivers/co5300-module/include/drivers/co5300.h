// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/gpio.h>

/**
 * @brief CO5300 AMOLED panel configuration.
 *
 * The panel is driven over QSPI through ESP-IDF's esp_lcd_co5300 component. All board-specific
 * bring-up data, including the vendor init sequence and the visible corner geometry, comes from
 * the devicetree so the driver itself stays reusable.
 */
struct Co5300Config {
    /** From display.yaml: a DisplayShapeType value. */
    uint8_t shape;
    /** From display.yaml: corner radius in pixels for DISPLAY_SHAPE_ROUNDED. */
    uint16_t corner_radius;

    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    int32_t gap_x;
    int32_t gap_y;

    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;
    uint32_t pixel_clock_hz;
    uint8_t transaction_queue_depth;

    struct GpioPinSpec pin_reset;

    /**
     * Optional tearing-effect (TE) input from the panel.
     *
     * When wired, the driver enables the panel's TE output and waits for a V-blank pulse before
     * starting each transfer. This keeps the panel from scanning a region while it is being
     * written, which is what produces the diagonal shear/"slanted text" artifact of an
     * unsynchronized flush. Leave unset to keep the previous free-running behaviour.
     */
    struct GpioPinSpec pin_te;
    /** Maximum wait for a TE pulse before falling back to an unsynchronized transfer. */
    uint32_t te_timeout_ms;
    /**
     * Present each refresh as one full-screen, frame-aligned transfer instead of tiling the
     * update. Required for tear-free output on panels whose refresh is slower than a partial
     * flush: LVGL's default vres/10 buffers would otherwise present a full-screen change as
     * ~10 flushes spread over ~10 frames.
     */
    bool full_frame;

    /**
     * Custom vendor init sequence, flattened as bytes:
     * [cmd, data_len, delay_ms, data_len bytes of data...] entries.
     * NULL/0 means the CO5300 component's built-in sequence is used.
     */
    const uint8_t* init_sequence;
    uint32_t init_sequence_length;
};

/**
 * @brief Backlight child configuration for a CO5300 panel.
 *
 * The panel has no dedicated backlight pin: brightness is controlled through the panel's
 * command interface (register 0x51). This device must be a child of the display it dims.
 */
struct Co5300BacklightConfig {
    /** Default brightness, 0-255. */
    uint8_t brightness_default;
};

#ifdef __cplusplus
}
#endif
