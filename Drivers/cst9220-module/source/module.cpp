// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cst9220_driver;

static Driver* const cst9220_drivers[] = {
    &cst9220_driver,
    nullptr
};

Module cst9220_module = {
    .name = "cst9220",
    .start = nullptr,
    .stop = nullptr,
    .drivers = cst9220_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
