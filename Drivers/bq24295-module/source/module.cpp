// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bq24295_driver;
extern Driver bq24295_power_supply_driver;

static Driver* const bq24295_drivers[] = {
    &bq24295_driver,
    &bq24295_power_supply_driver,
    nullptr
};

Module bq24295_module = {
    .name = "bq24295",
    .start = nullptr,
    .stop = nullptr,
    .drivers = bq24295_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
