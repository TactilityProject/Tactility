// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver co5300_driver;
extern Driver co5300_backlight_driver;

static Driver* const co5300_drivers[] = {
    &co5300_driver,
    &co5300_backlight_driver,
    nullptr
};

Module co5300_module = {
    .name = "co5300",
    .start = nullptr,
    .stop = nullptr,
    .drivers = co5300_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
