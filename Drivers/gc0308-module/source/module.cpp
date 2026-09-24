// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gc0308_driver;

static Driver* const gc0308_drivers[] = {
    &gc0308_driver,
    nullptr
};

Module gc0308_module = {
    .name = "gc0308",
    .start = nullptr,
    .stop = nullptr,
    .drivers = gc0308_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
