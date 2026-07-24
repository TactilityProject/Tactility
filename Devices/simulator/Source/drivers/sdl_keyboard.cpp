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
    sdl_input_pump();

    uint32_t key = 0;
    if (sdl_input_pop_key(&key)) {
        data->key = key;
        data->pressed = true;
        data->continue_reading = sdl_input_has_queued_key();
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }

    return ERROR_NONE;
}

// endregion

static const KeyboardApi sdl_keyboard_api = {
    .read_key = sdl_keyboard_read_key,
    .get_backlight = nullptr,
};

extern Module simulator_module;

Driver sdl_keyboard_driver = {
    .name = "sdl-keyboard",
    .compatible = (const char*[]) { "tactility,sdl-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &sdl_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &simulator_module,
    .internal = nullptr
};
