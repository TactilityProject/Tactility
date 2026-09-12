// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axs5106_driver;

static Driver* const axs5106_drivers[] = {
    &axs5106_driver,
    nullptr
};

Module axs5106_module = {
    .name = "axs5106",
    .start = nullptr,
    .stop = nullptr,
    .drivers = axs5106_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
