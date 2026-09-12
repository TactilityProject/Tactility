// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tca8418_driver;

static Driver* const tca8418_drivers[] = {
    &tca8418_driver,
    nullptr
};

Module tca8418_module = {
    .name = "tca8418",
    .start = nullptr,
    .stop = nullptr,
    .drivers = tca8418_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
