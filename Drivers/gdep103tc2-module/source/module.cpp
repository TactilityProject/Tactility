// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gdep103tc2_display_driver;

static Driver* const gdep103tc2_drivers[] = {
    &gdep103tc2_display_driver,
    nullptr
};

Module gdep103tc2_module = {
    .name = "gdep103tc2",
    .start = nullptr,
    .stop = nullptr,
    .drivers = gdep103tc2_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
