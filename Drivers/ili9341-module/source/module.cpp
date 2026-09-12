// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ili9341_driver;

static Driver* const ili9341_drivers[] = {
    &ili9341_driver,
    nullptr
};

Module ili9341_module = {
    .name = "ili9341",
    .start = nullptr,
    .stop = nullptr,
    .drivers = ili9341_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
