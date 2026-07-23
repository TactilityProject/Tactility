// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tdeck_keyboard_driver;
extern Driver tdeck_keyboard_backlight_driver;
extern Driver tdeck_trackball_driver;
extern Driver tpager_encoder_driver;

static Driver* const lilygo_drivers[] = {
    &tdeck_keyboard_driver,
    &tdeck_keyboard_backlight_driver,
    &tdeck_trackball_driver,
    &tpager_encoder_driver,
    nullptr
};

Module lilygo_module = {
    .name = "lilygo",
    .drivers = lilygo_drivers
};

} // extern "C"
