// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_bus.h"
#include "gdep103tc2_region.h"

#include <tactility/error.h>

#include <stdint.h>

struct Gdep103tc2Config;

/** Frame data of the panel. */
struct Gdep103tc2Frames {
    bool grayscale = false;
    /** Number of waveform passes */
    int passes = 0;
    /** Gray updates since start */
    uint32_t updates = 0;
    /**
     * Native panel orientation. 
     * - Black and white: 1 bit per pixel, 1 is black.
     * - Grayscale: 4 bits per pixel, 0 is black and 15 is white
     */
    uint8_t* target = nullptr;
    /** Black and white only */
    uint8_t* state = nullptr;
    /** Grayscale only */
    uint8_t* luts = nullptr;
    /** Rows in internal DMA capable memory */
    uint8_t* row_buf[2] = { nullptr, nullptr };
    uint8_t* zero_row = nullptr;
    uint8_t* solid_row = nullptr;
};

/** Confirms the configuration and allocates the buffers for the selected mode. */
error_t gdep103tc2_frames_init(Gdep103tc2Frames* frames, const Gdep103tc2Config* config);

void gdep103tc2_frames_deinit(Gdep103tc2Frames* frames);

/** Sets the whole target to white. */
void gdep103tc2_frames_clear_target(Gdep103tc2Frames* frames);

/** Declares the whole panel white, for example after a flash or something. Does nothing in grayscale mode. */
void gdep103tc2_frames_settle_white(Gdep103tc2Frames* frames);

/**
 * Streams one frame of the pixel state machine for the pixels in the region (columns are rounded out to
 * multiples of 8). Every pixel whose target changed is driven for DRIVE_FRAMES frames in a row.
 */
void gdep103tc2_run_update_frame(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames, const Gdep103tc2Region& region);

/** Brings the drivers back to neutral; needed after every sequence of frames. */
void gdep103tc2_run_neutral(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames);

/** Draws the 16 shades of the target in the region. */
void gdep103tc2_run_gray_update(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames, const Gdep103tc2Region& region);

/** Removes ghosting */
void gdep103tc2_run_flash(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames);
