// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gpio_trackball_driver;

static Driver* const gpio_trackball_drivers[] = {
    &gpio_trackball_driver,
    nullptr
};

Module gpio_trackball_module = {
    .name = "gpio-trackball",
    .start = nullptr,
    .stop = nullptr,
    .drivers = gpio_trackball_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
