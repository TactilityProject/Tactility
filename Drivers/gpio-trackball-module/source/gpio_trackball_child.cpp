// SPDX-License-Identifier: Apache-2.0
#include "gpio_trackball_child.h"

#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/mouse.h>
#include <tactility/drivers/trackball.h>

#include <algorithm>
#include <cstdlib>

#define GET_STATE(device) (static_cast<GpioTrackballChildState*>(const_cast<void*>((device)->config)))

// region Keyboard

// Reports one key event per read: the button as the enter key, and movement as arrow key presses along its strongest axis.
static error_t gpio_trackball_keyboard_read_key(Device* device, KeyboardKeyData* data) {
    auto* state = GET_STATE(device);

    int32_t dx = 0;
    int32_t dy = 0;
    trackball_read_delta(state->trackball, &dx, &dy);
    bool button_pressed = false;
    trackball_get_button_pressed(state->trackball, &button_pressed);

    if (dx != 0 || dy != 0) {
        uint32_t codepoint;
        if (std::abs(dx) >= std::abs(dy)) {
            codepoint = dx > 0 ? CODEPOINT_ARROW_RIGHT : CODEPOINT_ARROW_LEFT;
        } else {
            codepoint = dy > 0 ? CODEPOINT_ARROW_DOWN : CODEPOINT_ARROW_UP;
        }
        const int32_t steps = std::max(std::abs(dx), std::abs(dy)) * static_cast<int32_t>(state->sensitivity.load());
        // A change of direction drops the presses that weren't reported yet
        if (codepoint != state->pending_codepoint) {
            state->pending_codepoint = codepoint;
            state->pending_count = 0;
        }
        state->pending_count = std::clamp<int32_t>(state->pending_count + steps, 0, INT16_MAX);
    }

    data->key = 0;
    data->pressed = false;
    data->continue_reading = false;

    if (state->pressed_key == CODEPOINT_ENTER && button_pressed) {
        data->key = CODEPOINT_ENTER;
        data->pressed = true;
    } else if (state->pressed_key != 0) {
        // Every press is released before the next event, so consumers see each arrow key press
        data->key = state->pressed_key;
        data->pressed = false;
        state->pressed_key = 0;
        data->continue_reading = state->pending_count > 0;
    } else if (button_pressed) {
        state->pressed_key = CODEPOINT_ENTER;
        data->key = CODEPOINT_ENTER;
        data->pressed = true;
    } else if (state->pending_count > 0) {
        state->pending_count--;
        state->pressed_key = state->pending_codepoint;
        data->key = state->pressed_key;
        data->pressed = true;
    }
    return ERROR_NONE;
}

static const KeyboardApi GPIO_TRACKBALL_KEYBOARD_API = {
    .read_key = gpio_trackball_keyboard_read_key,
    .get_backlight = nullptr,
    .is_present = nullptr,
};

// endregion

// region Mouse

static error_t gpio_trackball_mouse_read_delta(Device* device, int32_t* out_dx, int32_t* out_dy) {
    auto* state = GET_STATE(device);
    error_t error = trackball_read_delta(state->trackball, out_dx, out_dy);
    const int32_t scale = state->sensitivity.load();
    *out_dx *= scale;
    *out_dy *= scale;
    return error;
}

static error_t gpio_trackball_mouse_get_button_pressed(Device* device, uint8_t, bool* out_pressed) {
    return trackball_get_button_pressed(GET_STATE(device)->trackball, out_pressed);
}

static const MouseApi GPIO_TRACKBALL_MOUSE_API = {
    .button_count = 1,
    .read_delta = gpio_trackball_mouse_read_delta,
    .get_button_pressed = gpio_trackball_mouse_get_button_pressed,
};

// endregion

extern "C" {

// Registered so driver_bind() has a valid ->internal, but never matched against a devicetree node:
// the trackball driver wires them up directly by pointer.
Driver gpio_trackball_keyboard_driver = {
    .name = "gpio_trackball_keyboard",
    .compatible = (const char*[]) { "tactility,gpio-trackball-keyboard", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &GPIO_TRACKBALL_KEYBOARD_API,
    .device_type = &KEYBOARD_TYPE,
    .internal = nullptr
};

Driver gpio_trackball_mouse_driver = {
    .name = "gpio_trackball_mouse",
    .compatible = (const char*[]) { "tactility,gpio-trackball-mouse", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &GPIO_TRACKBALL_MOUSE_API,
    .device_type = &MOUSE_TYPE,
    .internal = nullptr
};

}
