// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ft6x36_driver;

static Driver* const ft6x36_drivers[] = {
    &ft6x36_driver,
    nullptr
};

Module ft6x36_module = {
    .name = "ft6x36",
    .start = nullptr,
    .stop = nullptr,
    .drivers = ft6x36_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
