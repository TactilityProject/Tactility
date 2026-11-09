// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern Driver ssd1677_driver;

extern "C" {

static Driver* const ssd1677_drivers[] = {
    &ssd1677_driver,
    nullptr
};

Module ssd1677_module = {
    .name = "ssd1677",
    .start = nullptr,
    .stop = nullptr,
    .drivers = ssd1677_drivers,
    .symbols = nullptr,
    .internal = nullptr,
};

} // extern "C"
