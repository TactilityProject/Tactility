// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bq27427_driver;
extern Driver bq27427_power_supply_driver;

static Driver* const bq27427_drivers[] = {
    &bq27427_driver,
    &bq27427_power_supply_driver,
    nullptr
};

Module bq27427_module = {
    .name = "bq27427",
    .start = nullptr,
    .stop = nullptr,
    .drivers = bq27427_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
