// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver drv2605_driver;

static Driver* const drv2605_drivers[] = {
    &drv2605_driver,
    nullptr
};

Module drv2605_module = {
    .name = "drv2605",
    .drivers = drv2605_drivers
};

} // extern "C"
