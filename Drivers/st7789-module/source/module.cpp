// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7789_driver;

static Driver* const st7789_drivers[] = {
    &st7789_driver,
    nullptr
};

Module st7789_module = {
    .name = "st7789",
    .start = nullptr,
    .stop = nullptr,
    .drivers = st7789_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
