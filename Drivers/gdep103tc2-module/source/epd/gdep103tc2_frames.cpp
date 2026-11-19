// SPDX-License-Identifier: Apache-2.0
#include "gdep103tc2_frames.h"
#include "gdep103tc2_encode.h"
#include "gdep103tc2_gray.h"

#include <drivers/gdep103tc2.h>

#include <tactility/memory.h>

#include <esp_attr.h>
#include <esp_heap_caps.h>

#include <string.h>

// Bus codes for every pixel of a byte: 01 -> black, 10 -> white
static constexpr uint8_t SOLID_BLACK = 0x55;
static constexpr uint8_t SOLID_WHITE = 0xAA;

static constexpr int FLASH_CYCLES = 2;
static constexpr int FLASH_PASSES = 8;

static constexpr int MAX_GRAY_PASSES = 32;
static constexpr uint32_t SLOW_CLEAR_INTERVAL = 8;

static constexpr uint8_t TARGET_WHITE_BW = 0x00;
static constexpr uint8_t TARGET_WHITE_GRAY = 0xFF;

static constexpr size_t DMA_ROW_ALLOCATION = ROW_BYTES + 16;
static constexpr size_t DMA_ALIGNMENT = 16;

static void free_dma_rows(Gdep103tc2Frames* frames) {
    uint8_t** rows[] = { &frames->row_buf[0], &frames->row_buf[1], &frames->zero_row, &frames->solid_row };
    for (uint8_t** row : rows) {
        heap_caps_free(*row);
        *row = nullptr;
    }
}

static error_t resolve_gray_passes(const Gdep103tc2Config* config, int* out_passes) {
    *out_passes = 0;
    if (!config->grayscale) return ERROR_NONE;
    if (config->gray_waveform == nullptr || config->gray_waveform_length == 0 ||
        config->gray_waveform_length % GRAY_LEVELS != 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    const int passes = static_cast<int>(config->gray_waveform_length / GRAY_LEVELS);
    if (passes > MAX_GRAY_PASSES) return ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < config->gray_waveform_length; i++) {
        if (config->gray_waveform[i] > 2) return ERROR_INVALID_ARGUMENT;
    }
    *out_passes = passes;
    return ERROR_NONE;
}

error_t gdep103tc2_frames_init(Gdep103tc2Frames* frames, const Gdep103tc2Config* config) {
    int passes = 0;
    error_t error = resolve_gray_passes(config, &passes);
    if (error != ERROR_NONE) return error;

    static const MemoryPolicy external_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 16 };
    static const MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
    frames->grayscale = config->grayscale;
    frames->passes = passes;
    bool buffers_ok;
    if (config->grayscale) {
        frames->target = static_cast<uint8_t*>(memory_alloc_with_policy(GRAY_TARGET_SIZE, &external_policy));
        frames->luts = static_cast<uint8_t*>(memory_alloc_with_policy(GRAY_LUT_SIZE * passes, &internal_policy));
        buffers_ok = frames->target != nullptr && frames->luts != nullptr;
    } else {
        frames->target = static_cast<uint8_t*>(memory_alloc_with_policy(TARGET_SIZE, &external_policy));
        frames->state = static_cast<uint8_t*>(memory_alloc_with_policy(STATE_SIZE, &external_policy));
        buffers_ok = frames->target != nullptr && frames->state != nullptr;
    }

    uint8_t** rows[] = { &frames->row_buf[0], &frames->row_buf[1], &frames->zero_row, &frames->solid_row };
    bool rows_ok = true;
    for (uint8_t** row : rows) {
        *row = static_cast<uint8_t*>(heap_caps_aligned_calloc(DMA_ALIGNMENT, 1, DMA_ROW_ALLOCATION, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        rows_ok = rows_ok && *row != nullptr;
    }

    if (!buffers_ok || !rows_ok) {
        gdep103tc2_frames_deinit(frames);
        return ERROR_OUT_OF_MEMORY;
    }
    if (config->grayscale) {
        error = gdep103tc2_gray_build_luts(frames->luts, config->gray_waveform, passes);
        if (error != ERROR_NONE) {
            gdep103tc2_frames_deinit(frames);
            return error;
        }
    }
    gdep103tc2_frames_clear_target(frames);
    gdep103tc2_frames_settle_white(frames);
    return ERROR_NONE;
}

void gdep103tc2_frames_deinit(Gdep103tc2Frames* frames) {
    memory_free(frames->target);
    memory_free(frames->state);
    memory_free(frames->luts);
    frames->target = nullptr;
    frames->state = nullptr;
    frames->luts = nullptr;
    free_dma_rows(frames);
}

void gdep103tc2_frames_clear_target(Gdep103tc2Frames* frames) {
    if (frames->grayscale) {
        memset(frames->target, TARGET_WHITE_GRAY, GRAY_TARGET_SIZE);
    } else {
        memset(frames->target, TARGET_WHITE_BW, TARGET_SIZE);
    }
}

void gdep103tc2_frames_settle_white(Gdep103tc2Frames* frames) {
    if (frames->state != nullptr) {
        memset(frames->state, STATE_SETTLED_WHITE, STATE_SIZE);
    }
}

void IRAM_ATTR gdep103tc2_run_update_frame(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames, const Gdep103tc2Region& region) {
    memset(frames->row_buf[0], 0, ROW_BYTES);
    memset(frames->row_buf[1], 0, ROW_BYTES);

    gdep103tc2_bus_begin_pass(bus);
    int current = 0;
    for (int y = 0; y < PANEL_HEIGHT; y++) {
        const bool step = y != 0;
        if (y >= region.y0 && y < region.y1) {
            uint8_t* row = frames->row_buf[current];
            gdep103tc2_encode_row(frames->target + y * TARGET_ROW_BYTES, frames->state + y * STATE_ROW_BYTES, region.x0, region.x1, row);
            gdep103tc2_bus_send_row(bus, row, step, true);
            current ^= 1;
        } else {
            gdep103tc2_bus_send_row(bus, frames->zero_row, step, false);
        }
    }
    gdep103tc2_bus_end_pass(bus);
}

void gdep103tc2_run_neutral(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames) {
    gdep103tc2_bus_begin_pass(bus);
    for (int y = 0; y < PANEL_HEIGHT; y++) {
        gdep103tc2_bus_send_row(bus, frames->zero_row, y != 0, false);
    }
    gdep103tc2_bus_end_pass(bus);
}

// Just flashes until the panel shows properly
static void run_solid_passes(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames, uint8_t pattern, int passes, const Gdep103tc2Region& region) {
    const size_t first_byte = 2 * (ROW_WORDS - region.x1 / 8);
    const size_t byte_count = 2 * ((region.x1 - region.x0) / 8);
    memset(frames->solid_row, 0, ROW_PIXEL_BYTES);
    memset(frames->solid_row + first_byte, pattern, byte_count);
    for (int pass = 0; pass < passes; pass++) {
        gdep103tc2_bus_begin_pass(bus);
        for (int y = 0; y < PANEL_HEIGHT; y++) {
            const bool in_region = y >= region.y0 && y < region.y1;
            gdep103tc2_bus_send_row(bus, in_region ? frames->solid_row : frames->zero_row, y != 0, in_region);
        }
        gdep103tc2_bus_end_pass(bus);
    }
}

void gdep103tc2_run_flash(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames) {
    for (int cycle = 0; cycle < FLASH_CYCLES; cycle++) {
        run_solid_passes(bus, frames, SOLID_BLACK, FLASH_PASSES, FULL_REGION);
        run_solid_passes(bus, frames, SOLID_WHITE, FLASH_PASSES, FULL_REGION);
    }
    gdep103tc2_run_neutral(bus, frames);
}

static void run_gray_clear(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames, const Gdep103tc2Region& region, bool slow) {
    run_solid_passes(bus, frames, SOLID_BLACK, GRAY_CLEAR_PASSES, region);
    run_solid_passes(bus, frames, SOLID_WHITE, GRAY_CLEAR_PASSES, region);
    if (slow) {
        run_solid_passes(bus, frames, SOLID_BLACK, GRAY_CLEAR_PASSES, region);
        run_solid_passes(bus, frames, SOLID_WHITE, GRAY_CLEAR_PASSES, region);
    }
}

void IRAM_ATTR gdep103tc2_run_gray_update(Gdep103tc2Bus* bus, Gdep103tc2Frames* frames, const Gdep103tc2Region& region) {
    run_gray_clear(bus, frames, region, frames->updates % SLOW_CLEAR_INTERVAL == 0);
    frames->updates++;
    gdep103tc2_run_neutral(bus, frames);

    memset(frames->row_buf[0], 0, ROW_BYTES);
    memset(frames->row_buf[1], 0, ROW_BYTES);

    for (int pass = 0; pass < frames->passes; pass++) {
        const uint8_t* lut = frames->luts + pass * GRAY_LUT_SIZE;
        gdep103tc2_bus_begin_pass(bus);
        int current = 0;
        for (int y = 0; y < PANEL_HEIGHT; y++) {
            const bool step = y != 0;
            if (y >= region.y0 && y < region.y1) {
                // gotta be careful
                uint8_t* row = frames->row_buf[current];
                gdep103tc2_gray_encode_row(frames->target + y * GRAY_TARGET_ROW_BYTES, lut, region.x0, region.x1, row);
                gdep103tc2_bus_send_row(bus, row, step, true);
                current ^= 1;
            } else {
                gdep103tc2_bus_send_row(bus, frames->zero_row, step, false);
            }
        }
        gdep103tc2_bus_end_pass(bus);
    }
    gdep103tc2_run_neutral(bus, frames);
}
