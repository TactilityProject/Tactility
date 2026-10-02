// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Minimal driver for the ED047TC1 panel of the LILYGO T5 4.7 Inch E-Paper S3.
// The calls must be serialized by the user, the driver has no locking.

#pragma once

#include "t5s3_epd_rows.h"

#include <cstdint>

enum class T5s3EpdMode {
    // Black and white only
    Fast,
    // 16 levels in 30 phases, white pixels that stay white are not touched
    Quality,
    // 16 levels in 30 phases, white pixels that stay white are flashed too, which clears ghosting
    Full
};

// Allocates the frame buffers and sets up the pins and peripherals. The panel stays unpowered.
bool t5s3_epd_init(uint32_t pixel_clock_hz);

void t5s3_epd_deinit();

// Frame buffer to draw in, 4 bits per pixel. The panel shows it after t5s3_epd_update().
uint8_t* t5s3_epd_framebuffer();

void t5s3_epd_power_on();

void t5s3_epd_power_off();

// Drives all pixels that changed in the rows [y_start, y_end) since the last update. Needs the panel to be powered.
bool t5s3_epd_update(T5s3EpdMode mode, int32_t y_start, int32_t y_end);

void t5s3_epd_fill_white();

// Makes the next update drive every pixel
void t5s3_epd_invalidate();

// Flashes the whole panel to white. Needs the panel to be powered.
bool t5s3_epd_clear();

inline void t5s3_epd_set_pixel(uint8_t* framebuffer, int32_t x, int32_t y, uint8_t level) {
    if (x < 0 || x >= T5S3_EPD_WIDTH || y < 0 || y >= T5S3_EPD_HEIGHT) {
        return;
    }
    uint8_t* byte = &framebuffer[y * T5S3_EPD_FB_ROW_BYTES + x / 2];
    if (x % 2) {
        *byte = (*byte & 0x0F) | static_cast<uint8_t>(level << 4);
    } else {
        *byte = (*byte & 0xF0) | (level & 0x0F);
    }
}
