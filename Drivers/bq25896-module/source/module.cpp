// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bq25896_driver;
extern Driver bq25896_power_supply_driver;

static const Driver* bq25896_drivers[] = {
    &bq25896_driver,
    &bq25896_power_supply_driver,
    nullptr
};

Module bq25896_module = {
    .name = "bq25896",
    .drivers = bq25896_drivers
};

} // extern "C"
