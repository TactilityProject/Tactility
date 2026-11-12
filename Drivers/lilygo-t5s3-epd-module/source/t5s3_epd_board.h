// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Signalling of the ED047TC1 panel on the LILYGO T5 4.7 Inch E-Paper S3.
// The sequences and timings follow the epdiy project (https://github.com/vroland/epdiy).
// The calls must come from one task, there is no locking.

#pragma once

#include <cstdint>

constexpr int T5S3_EPD_WIDTH = 960;
constexpr int T5S3_EPD_HEIGHT = 540;

// A bus row holds 2 bits per pixel with the first of four pixels in the lowest bits.
// 00 and 11 hold a pixel, 01 drives it dark and 10 drives it light.
constexpr int T5S3_EPD_BUS_ROW_BYTES = T5S3_EPD_WIDTH / 4;

constexpr uint8_t T5S3_EPD_ALL_DARK = 0x55;
constexpr uint8_t T5S3_EPD_ALL_LIGHT = 0xAA;

// Fills the bus row of panel row y. Bytes the callback does not write keep their content.
typedef void (*T5s3EpdRowFn)(int y, uint8_t* row, void* context);

// Sets up the pins and peripherals. The panel stays unpowered.
bool t5s3_epd_board_init(uint32_t pixel_clock_hz);

void t5s3_epd_board_deinit();

void t5s3_epd_board_power_on();

void t5s3_epd_board_power_off();

// Sets every byte of both row buffers, which is what rows that the callback leaves alone contain
void t5s3_epd_board_fill_rows(uint8_t value);

// Drives one frame. The rows [y0, y1) that are set in rows get data from fill and are held for hold_ticks of 0.1 us,
// all other rows are skipped without driving their pixels. Without rows, all rows of the range get data.
// Returns false when a peripheral did not respond.
bool t5s3_epd_board_frame(int y0, int y1, const bool* rows, uint32_t hold_ticks, T5s3EpdRowFn fill, void* context);
