// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ssd1306_driver;

static Driver* const ssd1306_drivers[] = {
    &ssd1306_driver,
    nullptr
};

Module ssd1306_module = {
    .name = "ssd1306",
    .start = nullptr,
    .stop = nullptr,
    .drivers = ssd1306_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
