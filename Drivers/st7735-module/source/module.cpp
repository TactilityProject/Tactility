// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7735_driver;

static Driver* const st7735_drivers[] = {
    &st7735_driver,
    nullptr
};

Module st7735_module = {
    .name = "st7735",
    .start = nullptr,
    .stop = nullptr,
    .drivers = st7735_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
