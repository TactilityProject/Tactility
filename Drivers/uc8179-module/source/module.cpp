// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern Driver uc8179_driver;

extern "C" {

static Driver* const uc8179_drivers[] = {
    &uc8179_driver,
    nullptr
};

Module uc8179_module = {
    .name = "uc8179",
    .start = nullptr,
    .stop = nullptr,
    .drivers = uc8179_drivers,
    .symbols = nullptr,
    .internal = nullptr,
};

} // extern "C"
