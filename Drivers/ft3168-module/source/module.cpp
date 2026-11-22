// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ft3168_driver;

static Driver* const ft3168_drivers[] = {
    &ft3168_driver,
    nullptr
};

Module ft3168_module = {
    .name = "ft3168",
    .start = nullptr,
    .stop = nullptr,
    .drivers = ft3168_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
