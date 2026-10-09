// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7305_driver;

static Driver* const st7305_drivers[] = {
    &st7305_driver,
    nullptr
};

Module st7305_module = {
    .name = "st7305",
    .start = nullptr,
    .stop = nullptr,
    .drivers = st7305_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
