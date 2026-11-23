// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver trmnl_x_power_driver;
extern Driver trmnl_x_power_supply_driver;

static Driver* const trmnl_x_drivers[] = {
    &trmnl_x_power_driver,
    &trmnl_x_power_supply_driver,
    nullptr
};

Module trmnl_trmnl_x_module = {
    .name = "trmnl-trmnl-x",
    .start = nullptr,
    .stop = nullptr,
    .drivers = trmnl_x_drivers,
    .symbols = nullptr,
    .internal = nullptr
};
}
