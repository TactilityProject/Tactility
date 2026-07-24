// SPDX-License-Identifier: Apache-2.0
#include "sdl_input.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/pointer.h>
#include <tactility/module.h>

// region Driver lifecycle

static error_t start(Device*) { return ERROR_NONE; }
static error_t stop(Device*) { return ERROR_NONE; }

// endregion

// region PointerApi

static error_t sdl_pointer_read_data(Device*, TickType_t) {
    sdl_input_pump();
    return ERROR_NONE;
}

static bool sdl_pointer_get_touched_points(Device*, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    SdlPointerState state;
    sdl_input_get_pointer_state(&state);

    if (!state.pressed || max_point_count == 0) {
        *point_count = 0;
        return false;
    }

    x[0] = static_cast<uint16_t>(state.x);
    y[0] = static_cast<uint16_t>(state.y);
    if (strength != nullptr) {
        strength[0] = 0xFFFF;
    }
    *point_count = 1;
    return true;
}

// endregion

static const PointerApi sdl_pointer_api = {
    .enter_sleep = nullptr,
    .exit_sleep = nullptr,
    .read_data = sdl_pointer_read_data,
    .get_touched_points = sdl_pointer_get_touched_points,
    .set_swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .set_mirror_x = nullptr,
    .get_mirror_x = nullptr,
    .set_mirror_y = nullptr,
    .get_mirror_y = nullptr,
};

extern Module simulator_module;

Driver sdl_pointer_driver = {
    .name = "sdl-pointer",
    .compatible = (const char*[]) { "tactility,sdl-pointer", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &sdl_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &simulator_module,
    .internal = nullptr
};
