// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver sy6970_driver;
extern Driver sy6970_power_supply_driver;

static Driver* const sy6970_drivers[] = {
    &sy6970_driver,
    &sy6970_power_supply_driver,
    nullptr
};

Module sy6970_module = {
    .name = "sy6970",
    .drivers = sy6970_drivers
};

} // extern "C"
