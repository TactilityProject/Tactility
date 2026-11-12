// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/device.h>
#include <tactility/driver.h>

#include <atomic>
#include <cstdint>

/** State of a child device. It is the child's `config` and is owned by the trackball device. */
struct GpioTrackballChildState {
    struct Device* trackball = nullptr;
    // Raw trackball ticks to key presses (keyboard child) or pixels (mouse child)
    std::atomic<uint8_t> sensitivity { 1 };
    // Keyboard child: arrow key presses that are still to be reported
    uint32_t pending_codepoint = 0;
    int32_t pending_count = 0;
    // Keyboard child: the key that is reported as pressed, or 0
    uint32_t pressed_key = 0;
};

extern "C" {

extern Driver gpio_trackball_keyboard_driver;
extern Driver gpio_trackball_mouse_driver;

}
