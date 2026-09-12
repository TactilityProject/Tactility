// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tdeck_keyboard_driver;
extern Driver tdeck_keyboard_backlight_driver;

static Driver* const lilygo_drivers[] = {
    &tdeck_keyboard_driver,
    &tdeck_keyboard_backlight_driver,
    nullptr
};

Module lilygo_module = {
    .name = "lilygo",
    .start = nullptr,
    .stop = nullptr,
    .drivers = lilygo_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
