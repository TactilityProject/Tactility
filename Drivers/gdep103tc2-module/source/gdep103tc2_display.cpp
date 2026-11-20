// SPDX-License-Identifier: Apache-2.0
#include <drivers/gdep103tc2.h>
#include "epd/gdep103tc2_encode.h"
#include "epd/gdep103tc2_engine.h"
#include "epd/gdep103tc2_gray.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <algorithm>
#include <new>

#define TAG "Gdep103tc2Display"
#define GET_CONFIG(device) (static_cast<const Gdep103tc2Config*>((device)->config))

static bool is_portrait(uint32_t rotation) {
    return rotation == 90 || rotation == 270;
}

// Rotated coordinates to native panel coordinates
static inline void to_native(uint32_t rotation, int x, int y, int* native_x, int* native_y) {
    switch (rotation) {
        case 90:
            *native_x = PANEL_WIDTH - 1 - y;
            *native_y = x;
            break;
        case 180:
            *native_x = PANEL_WIDTH - 1 - x;
            *native_y = PANEL_HEIGHT - 1 - y;
            break;
        case 270:
            *native_x = y;
            *native_y = PANEL_HEIGHT - 1 - x;
            break;
        default:
            *native_x = x;
            *native_y = y;
            break;
    }
}

extern "C" {

struct Gdep103tc2Internal {
    Gdep103tc2Engine engine;
    uint32_t rotation;
    bool grayscale;
};

static Gdep103tc2Engine* engine_of(Device* device) {
    return &static_cast<Gdep103tc2Internal*>(device_get_driver_data(device))->engine;
}

// region DisplayApi

static error_t gdep103tc2_display_reset(Device* device) {
    gdep103tc2_engine_power_off(engine_of(device));
    return ERROR_NONE;
}

static error_t gdep103tc2_display_init(Device*) {
    return ERROR_NONE;
}

static error_t gdep103tc2_display_clear(Device* device) {
    Gdep103tc2Engine* engine = engine_of(device);
    gdep103tc2_frames_clear_target(&engine->frames);
    return gdep103tc2_engine_flash(engine, true);
}

static error_t gdep103tc2_display_refresh(Device* device) {
    return gdep103tc2_engine_flash(engine_of(device), false);
}

static void store_black_white(uint8_t* target, int native_x, int native_y, uint8_t luminance, int32_t x, int32_t y) {
    uint8_t* byte = target + native_y * TARGET_ROW_BYTES + native_x / 8;
    const uint8_t mask = 0x80 >> (native_x & 7);
    if (gdep103tc2_dither_is_black(luminance, x, y)) {
        *byte |= mask;
    } else {
        *byte &= ~mask;
    }
}

static void store_gray(uint8_t* target, int native_x, int native_y, uint8_t luminance) {
    uint8_t* byte = target + native_y * GRAY_TARGET_ROW_BYTES + native_x / 2;
    const uint8_t level = gdep103tc2_gray_level_from_luminance(luminance);
    if (native_x & 1) {
        *byte = (*byte & 0xF0) | level;
    } else {
        *byte = (*byte & 0x0F) | (level << 4);
    }
}

// Reports GRAYSCALE8 so LVGL renders partial tiles
static error_t gdep103tc2_display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Gdep103tc2Internal*>(device_get_driver_data(device));
    uint8_t* target = internal->engine.frames.target;
    const uint32_t rotation = internal->rotation;
    const bool grayscale = internal->grayscale;
    const int32_t width = x_end - x_start;
    const auto* source = static_cast<const uint8_t*>(color_data);

    for (int32_t row = 0; row < width * (y_end - y_start); row += width) {
        const int32_t y = y_start + row / width;
        for (int32_t col = 0; col < width; col++) {
            const int32_t x = x_start + col;
            int native_x, native_y;
            to_native(rotation, x, y, &native_x, &native_y);
            if (grayscale) {
                store_gray(target, native_x, native_y, source[row + col]);
            } else {
                store_black_white(target, native_x, native_y, source[row + col], x, y);
            }
        }
    }

    int x0, y0, x1, y1;
    to_native(rotation, x_start, y_start, &x0, &y0);
    to_native(rotation, x_end - 1, y_end - 1, &x1, &y1);
    gdep103tc2_engine_submit(&internal->engine, { std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + 1, std::max(y0, y1) + 1 });
    return ERROR_NONE;
}

static error_t gdep103tc2_display_disp_on_off(Device* device, bool on_off) {
    if (!on_off) {
        gdep103tc2_engine_power_off(engine_of(device));
    }
    return ERROR_NONE;
}

static DisplayColorFormat gdep103tc2_display_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_GRAYSCALE8;
}

static uint16_t gdep103tc2_display_get_resolution_x(Device* device) {
    return is_portrait(GET_CONFIG(device)->rotation) ? PANEL_HEIGHT : PANEL_WIDTH;
}

static uint16_t gdep103tc2_display_get_resolution_y(Device* device) {
    return is_portrait(GET_CONFIG(device)->rotation) ? PANEL_WIDTH : PANEL_HEIGHT;
}

// endregion

static constexpr DisplayApi gdep103tc2_display_api = {
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH,
    .reset = gdep103tc2_display_reset,
    .init = gdep103tc2_display_init,
    .draw_bitmap = gdep103tc2_display_draw_bitmap,
    .clear = gdep103tc2_display_clear,
    .refresh = gdep103tc2_display_refresh,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = gdep103tc2_display_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = gdep103tc2_display_get_color_format,
    .get_resolution_x = gdep103tc2_display_get_resolution_x,
    .get_resolution_y = gdep103tc2_display_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

// region Driver lifecycle

static bool is_valid_rotation(uint32_t rotation) {
    return rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270;
}

static error_t start(Device* device) {
    const Gdep103tc2Config* config = GET_CONFIG(device);
    if (!is_valid_rotation(config->rotation)) {
        LOG_E(TAG, "Rotation %lu is not supported", static_cast<unsigned long>(config->rotation));
        return ERROR_INVALID_ARGUMENT;
    }

    // The engine holds a spinlock, so it lives in internal memory
    static const MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
    void* storage = memory_calloc_with_policy(1, sizeof(Gdep103tc2Internal), &internal_policy);
    if (storage == nullptr) return ERROR_OUT_OF_MEMORY;
    auto* internal = new (storage) Gdep103tc2Internal();
    internal->rotation = config->rotation;
    internal->grayscale = config->grayscale;

    error_t error = gdep103tc2_engine_start(&internal->engine, config);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start the display engine: %d", error);
        internal->~Gdep103tc2Internal();
        memory_free(internal);
        return error;
    }

    device_set_driver_data(device, internal);

    // The boot splash
    gdep103tc2_engine_flash(&internal->engine, true);

    LOG_I(TAG, "Started (%dx%d, rotation %lu, %s)", PANEL_WIDTH, PANEL_HEIGHT, static_cast<unsigned long>(internal->rotation), internal->grayscale ? "16 shades" : "black and white");
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Gdep103tc2Internal*>(device_get_driver_data(device));
    gdep103tc2_engine_stop(&internal->engine);
    device_set_driver_data(device, nullptr);
    internal->~Gdep103tc2Internal();
    memory_free(internal);
    return ERROR_NONE;
}

// endregion

Driver gdep103tc2_display_driver = {
    .name = "gdep103tc2",
    .compatible = (const char*[]) { "gooddisplay,gdep103tc2", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &gdep103tc2_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};

}
