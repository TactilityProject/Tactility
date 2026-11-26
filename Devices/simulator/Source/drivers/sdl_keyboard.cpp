// SPDX-License-Identifier: Apache-2.0
#include "sdl_input.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/module.h>

// region Driver lifecycle

static error_t start(Device*) { return ERROR_NONE; }
static error_t stop(Device*) { return ERROR_NONE; }

// endregion

// region KeyboardApi

static error_t sdl_keyboard_read_key(Device*, KeyboardKeyData* data) {
    if (sdl_input_pop_key(data)) {
        data->continue_reading = sdl_input_has_queued_key();
    } else if (!sdl_input_get_held_key(data)) {
        // Otherwise there was no fresh event this poll, but the key LVGL last saw pressed is still physically down
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }

    return ERROR_NONE;
}

// endregion

static const KeyboardApi sdl_keyboard_api = {
    .capabilities = 0,
    .read_key = sdl_keyboard_read_key,
    .get_backlight = nullptr,
};

extern Module simulator_module;

Driver sdl_keyboard_driver = {
    .name = "sdl-keyboard",
    .compatible = (const char*[]) { "tactility,sdl-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &sdl_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .internal = nullptr,
};
