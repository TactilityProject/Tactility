// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_pins.h"

#include <tactility/error.h>

#include <esp_lcd_panel_io.h>

#include <stdint.h>

struct Gdep103tc2Bus {
    Gdep103tc2Pins pins;
    uint32_t pixel_clock_hz = 0;
    esp_lcd_i80_bus_handle_t bus = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    volatile bool dma_done = true;
    /** Rows that could not be queued */
    volatile uint32_t errors = 0;
    int64_t row_selected_us = 0;
    uint32_t row_dwell_us = 0;
};

/** Configures the control lines and creates the parallel bus, just init */
error_t gdep103tc2_bus_init(Gdep103tc2Bus* bus, const Gdep103tc2Pins& pins, uint32_t pixel_clock_hz);

void gdep103tc2_bus_deinit(Gdep103tc2Bus* bus);

/** Starts a pass */
void gdep103tc2_bus_begin_pass(Gdep103tc2Bus* bus);

/**
 * Sends one row of ROW_BYTES bytes, while waiting for the previous row first
 * @param[in] row DMA capable internal memory
 * @param[in] step false for the first row of a pass, true for the rows after it
 * @param[in] drive true when the row drives pixels. The gate then stays selected for at least
 * ROW_DRIVE_DWELL_US, as that is the dose a pixel gets in one pass.
 */
void gdep103tc2_bus_send_row(Gdep103tc2Bus* bus, const uint8_t* row, bool step, bool drive);

/** End a pass */
void gdep103tc2_bus_end_pass(Gdep103tc2Bus* bus);

/** Leaves the gate and source driver control lines in their safe, idke state. */
void gdep103tc2_bus_power_down(Gdep103tc2Bus* bus);

/** Returns the number of rows that failed since the previous call and resets the counter */
uint32_t gdep103tc2_bus_take_errors(Gdep103tc2Bus* bus);
