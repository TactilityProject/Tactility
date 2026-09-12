// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7121_driver;

static Driver* const st7121_drivers[] = {
    &st7121_driver,
    nullptr
};

Module st7121_module = {
    .name = "st7121",
    .start = nullptr,
    .stop = nullptr,
    .drivers = st7121_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
