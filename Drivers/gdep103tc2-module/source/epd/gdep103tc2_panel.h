// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

// Geometry of the 10.3" panel and the timing of its row control signals.
// See: https://github.com/bitbank2/FastEPD

static constexpr int PANEL_WIDTH = 1872;
static constexpr int PANEL_HEIGHT = 1404;
static constexpr int BUS_WIDTH = 16;

// Bus words (8 pixels) per row, without the padding
static constexpr size_t ROW_WORDS = PANEL_WIDTH / 8;

// A row is 2 bits per pixel, followed by padding that the source driver needs after the pixels
static constexpr size_t ROW_PIXEL_BYTES = PANEL_WIDTH / 4;
static constexpr size_t LINE_PAD_BYTES = 44;
static constexpr size_t ROW_BYTES = ROW_PIXEL_BYTES + LINE_PAD_BYTES;

// Row start sequence: SPV pulse while CKV is clocked, in microseconds
static constexpr uint32_t START_CKV_HIGH_US = 7;
static constexpr uint32_t START_SPV_LOW_US = 10;
static constexpr uint32_t START_SPV_RISE_US = 8;
static constexpr uint32_t START_PULSE_US = 10;
static constexpr int START_EXTRA_CKV_PULSES = 3;

// Time the gate of a row that drives pixels stays selected
static constexpr uint32_t ROW_DRIVE_DWELL_US = 22;

// Time the last row of a pass needs to be applied before the next pass begins
static constexpr uint32_t PASS_END_DELAY_US = 230;
