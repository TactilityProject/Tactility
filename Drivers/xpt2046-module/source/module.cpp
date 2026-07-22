// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver xpt2046_driver;
extern Driver xpt2046_power_supply_driver;

static Driver* const xpt2046_drivers[] = {
    &xpt2046_driver,
    &xpt2046_power_supply_driver,
    nullptr
};

Module xpt2046_module = {
    .name = "xpt2046",
    .drivers = xpt2046_drivers
};

} // extern "C"
