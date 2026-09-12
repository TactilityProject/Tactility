// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gt911_driver;

static Driver* const gt911_drivers[] = {
    &gt911_driver,
    nullptr
};

Module gt911_module = {
    .name = "gt911",
    .start = nullptr,
    .stop = nullptr,
    .drivers = gt911_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
